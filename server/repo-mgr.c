/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <glib/gstdio.h>

#include <openssl/sha.h>
#include <openssl/rand.h>

#include <ccnet.h>
#include <ccnet/ccnet-object.h>
#include "utils.h"
#include "log.h"

#include "syncwerk-session.h"
#include "commit-mgr.h"
#include "branch-mgr.h"
#include "repo-mgr.h"
#include "fs-mgr.h"
#include "syncwerk-error.h"
#include "syncwerk-crypt.h"

#include "syncwerk-server-db.h"

#define REAP_TOKEN_INTERVAL 300 /* 5 mins */
#define DECRYPTED_TOKEN_TTL 3600 /* 1 hour */
#define SCAN_TRASH_DAYS 1 /* one day */
#define TRASH_EXPIRE_DAYS 30 /* one month */

typedef struct DecryptedToken {
    char *token;
    gint64 reap_time;
} DecryptedToken;

struct _SyncwRepoManagerPriv {
    /* (encrypted_token, session_key) -> decrypted token */
    GHashTable *decrypted_tokens;
    pthread_rwlock_t lock;
    CcnetTimer *reap_token_timer;

    CcnetTimer *scan_trash_timer;
};

static void
load_repo (SyncwRepoManager *manager, SyncwRepo *repo);

static int create_db_tables_if_not_exist (SyncwRepoManager *mgr);

static int save_branch_repo_map (SyncwRepoManager *manager, SyncwBranch *branch);

static int reap_token (void *data);
static void decrypted_token_free (DecryptedToken *token);

gboolean
is_repo_id_valid (const char *id)
{
    if (!id)
        return FALSE;

    return is_uuid_valid (id);
}

SyncwRepo*
syncw_repo_new (const char *id, const char *name, const char *desc)
{
    SyncwRepo* repo;

    /* valid check */
  
    
    repo = g_new0 (SyncwRepo, 1);
    memcpy (repo->id, id, 36);
    repo->id[36] = '\0';

    repo->name = g_strdup(name);
    repo->desc = g_strdup(desc);

    repo->ref_cnt = 1;

    return repo;
}

void
syncw_repo_free (SyncwRepo *repo)
{
    if (repo->name) g_free (repo->name);
    if (repo->desc) g_free (repo->desc);
    if (repo->head) syncw_branch_unref (repo->head);
    if (repo->virtual_info)
        syncw_virtual_repo_info_free (repo->virtual_info);
    g_free (repo->last_modifier);
    g_free (repo);
}

void
syncw_repo_ref (SyncwRepo *repo)
{
    g_atomic_int_inc (&repo->ref_cnt);
}

void
syncw_repo_unref (SyncwRepo *repo)
{
    if (!repo)
        return;

    if (g_atomic_int_dec_and_test (&repo->ref_cnt))
        syncw_repo_free (repo);
}

static void
set_head_common (SyncwRepo *repo, SyncwBranch *branch)
{
    if (repo->head)
        syncw_branch_unref (repo->head);
    repo->head = branch;
    syncw_branch_ref(branch);
}

int
syncw_repo_set_head (SyncwRepo *repo, SyncwBranch *branch)
{
    if (save_branch_repo_map (repo->manager, branch) < 0)
        return -1;
    set_head_common (repo, branch);
    return 0;
}

void
syncw_repo_from_commit (SyncwRepo *repo, SyncwCommit *commit)
{
    repo->name = g_strdup (commit->repo_name);
    repo->desc = g_strdup (commit->repo_desc);
    repo->encrypted = commit->encrypted;
    repo->repaired = commit->repaired;
    repo->last_modify = commit->ctime;
    memcpy (repo->root_id, commit->root_id, 40);
    if (repo->encrypted) {
        repo->enc_version = commit->enc_version;
        if (repo->enc_version == 1)
            memcpy (repo->magic, commit->magic, 32);
        else if (repo->enc_version == 2) {
            memcpy (repo->magic, commit->magic, 64);
            memcpy (repo->random_key, commit->random_key, 96);
        }
    }
    repo->no_local_history = commit->no_local_history;
    repo->version = commit->version;
    repo->last_modifier = g_strdup (commit->creator_name);
}

void
syncw_repo_to_commit (SyncwRepo *repo, SyncwCommit *commit)
{
    commit->repo_name = g_strdup (repo->name);
    commit->repo_desc = g_strdup (repo->desc);
    commit->encrypted = repo->encrypted;
    commit->repaired = repo->repaired;
    if (commit->encrypted) {
        commit->enc_version = repo->enc_version;
        if (commit->enc_version == 1)
            commit->magic = g_strdup (repo->magic);
        else if (commit->enc_version == 2) {
            commit->magic = g_strdup (repo->magic);
            commit->random_key = g_strdup (repo->random_key);
        }
    }
    commit->no_local_history = repo->no_local_history;
    commit->version = repo->version;
}

static gboolean
collect_commit (SyncwCommit *commit, void *vlist, gboolean *stop)
{
    GList **commits = vlist;

    /* The traverse function will unref the commit, so we need to ref it.
     */
    syncw_commit_ref (commit);
    *commits = g_list_prepend (*commits, commit);
    return TRUE;
}

GList *
syncw_repo_get_commits (SyncwRepo *repo)
{
    GList *branches;
    GList *ptr;
    SyncwBranch *branch;
    GList *commits = NULL;

    branches = syncw_branch_manager_get_branch_list (syncw->branch_mgr, repo->id);
    if (branches == NULL) {
        syncw_warning ("Failed to get branch list of repo %s.\n", repo->id);
        return NULL;
    }

    for (ptr = branches; ptr != NULL; ptr = ptr->next) {
        branch = ptr->data;
        gboolean res = syncw_commit_manager_traverse_commit_tree (syncw->commit_mgr,
                                                                 repo->id,
                                                                 repo->version,
                                                                 branch->commit_id,
                                                                 collect_commit,
                                                                 &commits,
                                                                 FALSE);
        if (!res) {
            for (ptr = commits; ptr != NULL; ptr = ptr->next)
                syncw_commit_unref ((SyncwCommit *)(ptr->data));
            g_list_free (commits);
            goto out;
        }
    }

    commits = g_list_reverse (commits);

out:
    for (ptr = branches; ptr != NULL; ptr = ptr->next) {
        syncw_branch_unref ((SyncwBranch *)ptr->data);
    }
    return commits;
}

gboolean
should_ignore_file(const char *filename, void *data)
{
    /* GPatternSpec **spec = ignore_patterns; */

    if (!g_utf8_validate (filename, -1, NULL)) {
        syncw_warning ("File name %s contains non-UTF8 characters, skip.\n", filename);
        return TRUE;
    }

    /* Ignore file/dir if its name is too long. */
    if (strlen(filename) >= SYNCW_DIR_NAME_LEN)
        return TRUE;

    if (strchr (filename, '/'))
        return TRUE;

    return FALSE;
}

static gboolean
collect_repo_id (SyncwDBRow *row, void *data);

static int
scan_trash (void *data)
{
    GList *repo_ids = NULL;
    SyncwRepoManager *mgr = syncw->repo_mgr;
    gint64 trash_expire_interval = TRASH_EXPIRE_DAYS * 24 * 3600;
    int expire_days = syncw_cfg_manager_get_config_int (syncw->cfg_mgr,
                                                       "library_trash",
                                                       "expire_days");
    if (expire_days > 0) {
        trash_expire_interval = expire_days * 24 * 3600;
    }

    gint64 expire_time = time(NULL) - trash_expire_interval;
    char *sql = "SELECT repo_id FROM RepoTrash WHERE del_time <= ?";

    int ret = syncwerk_server_db_statement_foreach_row (syncw->db, sql,
                                             collect_repo_id, &repo_ids,
                                             1, "int64", expire_time);
    if (ret < 0) {
        syncw_warning ("Get expired repo from trash failed.");
        string_list_free (repo_ids);
        return TRUE;
    }

    GList *iter;
    char *repo_id;
    for (iter=repo_ids; iter; iter=iter->next) {
        repo_id = iter->data;
        ret = syncw_repo_manager_del_repo_from_trash (mgr, repo_id, NULL);
        if (ret < 0)
            break;
    }

    string_list_free (repo_ids);

    return TRUE;
}

static void
init_scan_trash_timer (SyncwRepoManagerPriv *priv, GKeyFile *config)
{
    int scan_days;
    GError *error = NULL;

    scan_days = g_key_file_get_integer (config,
                                        "library_trash", "scan_days",
                                        &error);
    if (error) {
       scan_days = SCAN_TRASH_DAYS;
       g_clear_error (&error);
    }

    priv->scan_trash_timer = ccnet_timer_new (scan_trash, NULL,
                                              scan_days * 24 * 3600 * 1000);
}

SyncwRepoManager*
syncw_repo_manager_new (SyncwerkSession *syncw)
{
    SyncwRepoManager *mgr = g_new0 (SyncwRepoManager, 1);

    mgr->priv = g_new0 (SyncwRepoManagerPriv, 1);
    mgr->syncw = syncw;

    mgr->priv->decrypted_tokens = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                         g_free,
                                                         (GDestroyNotify)decrypted_token_free);
    pthread_rwlock_init (&mgr->priv->lock, NULL);
    mgr->priv->reap_token_timer = ccnet_timer_new (reap_token, mgr,
                                                   REAP_TOKEN_INTERVAL * 1000);

    init_scan_trash_timer (mgr->priv, syncw->config);

    return mgr;
}

int
syncw_repo_manager_init (SyncwRepoManager *mgr)
{
    /* On the server, we load repos into memory on-demand, because
     * there are too many repos.
     */
    if (create_db_tables_if_not_exist (mgr) < 0) {
        syncw_warning ("[repo mgr] failed to create tables.\n");
        return -1;
    }

    if (syncw_repo_manager_init_merge_scheduler() < 0) {
        syncw_warning ("Failed to init merge scheduler.\n");
        return -1;
    }

    return 0;
}

int
syncw_repo_manager_start (SyncwRepoManager *mgr)
{
    return 0;
}

int
syncw_repo_manager_add_repo (SyncwRepoManager *manager,
                            SyncwRepo *repo)
{
    SyncwDB *db = manager->syncw->db;

    if (syncwerk_server_db_statement_query (db, "INSERT INTO Repo (repo_id) VALUES (?)",
                                 1, "string", repo->id) < 0)
        return -1;

    repo->manager = manager;

    return 0;
}

static int
add_deleted_repo_record (SyncwRepoManager *mgr, const char *repo_id)
{
    if (syncwerk_server_db_type(syncw->db) == SYNCW_DB_TYPE_PGSQL) {
        gboolean exists, err;

        exists = syncwerk_server_db_statement_exists (syncw->db,
                                           "SELECT repo_id FROM GarbageRepos "
                                           "WHERE repo_id=?",
                                           &err, 1, "string", repo_id);
        if (err)
            return -1;

        if (!exists) {
            return syncwerk_server_db_statement_query(syncw->db,
                                           "INSERT INTO GarbageRepos (repo_id) VALUES (?)",
                                           1, "string", repo_id);
        }

        return 0;
    } else {
        return syncwerk_server_db_statement_query (syncw->db,
                                        "REPLACE INTO GarbageRepos (repo_id) VALUES (?)",
                                        1, "string", repo_id);
    }
}

static int
add_deleted_repo_to_trash (SyncwRepoManager *mgr, const char *repo_id,
                           SyncwCommit *commit)
{
    char *owner = NULL;
    int ret = -1;

    owner = syncw_repo_manager_get_repo_owner (mgr, repo_id);
    if (!owner) {
        syncw_warning ("Failed to get owner for repo %.8s.\n", repo_id);
        goto out;
    }

    gint64 size = syncw_repo_manager_get_repo_size (mgr, repo_id);
    if (size == -1) {
        syncw_warning ("Failed to get size of repo %.8s.\n", repo_id);
        goto out;
    }

    ret =  syncwerk_server_db_statement_query (mgr->syncw->db,
                                    "INSERT INTO RepoTrash (repo_id, repo_name, head_id, "
                                    "owner_id, size, org_id, del_time) "
                                    "values (?, ?, ?, ?, ?, -1, ?)", 6,
                                    "string", repo_id,
                                    "string", commit->repo_name,
                                    "string", commit->commit_id,
                                    "string", owner,
                                    "int64", size,
                                    "int64", (gint64)time(NULL));
out:
    g_free (owner);

    return ret;
}

static int
remove_virtual_repo_ondisk (SyncwRepoManager *mgr,
                            const char *repo_id)
{
    SyncwDB *db = mgr->syncw->db;

    /* Remove record in repo table first.
     * Once this is commited, we can gc the other tables later even if
     * we're interrupted.
     */
    if (syncwerk_server_db_statement_query (db, "DELETE FROM Repo WHERE repo_id = ?",
                                 1, "string", repo_id) < 0)
        return -1;

    /* remove branch */
    GList *p;
    GList *branch_list = 
        syncw_branch_manager_get_branch_list (syncw->branch_mgr, repo_id);
    for (p = branch_list; p; p = p->next) {
        SyncwBranch *b = (SyncwBranch *)p->data;
        syncw_repo_manager_branch_repo_unmap (mgr, b);
        syncw_branch_manager_del_branch (syncw->branch_mgr, repo_id, b->name);
    }
    syncw_branch_list_free (branch_list);

    syncwerk_server_db_statement_query (db, "DELETE FROM RepoOwner WHERE repo_id = ?",
                   1, "string", repo_id);

    syncwerk_server_db_statement_query (db, "DELETE FROM SharedRepo WHERE repo_id = ?",
                   1, "string", repo_id);

    syncwerk_server_db_statement_query (db, "DELETE FROM RepoGroup WHERE repo_id = ?",
                   1, "string", repo_id);

    if (!syncw->cloud_mode) {
        syncwerk_server_db_statement_query (db, "DELETE FROM InnerPubRepo WHERE repo_id = ?",
                                 1, "string", repo_id);
    }

    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoUserToken WHERE repo_id = ?",
                             1, "string", repo_id);

    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoValidSince WHERE repo_id = ?",
                             1, "string", repo_id);

    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoSize WHERE repo_id = ?",
                             1, "string", repo_id);

    /* For GC commit objects for this virtual repo. Fs and blocks are GC
     * from the parent repo.
     */
    add_deleted_repo_record (mgr, repo_id);

    return 0;
}

static gboolean
get_branch (SyncwDBRow *row, void *vid)
{
    char *ret = vid;
    const char *commit_id;

    commit_id = syncwerk_server_db_row_get_column_text (row, 0);
    memcpy (ret, commit_id, 41);

    return FALSE;
}

static SyncwCommit*
get_head_commit (SyncwRepoManager *mgr, const char *repo_id, gboolean *has_err)
{
    char commit_id[41];
    char *sql;

    commit_id[0] = 0;
    sql = "SELECT commit_id FROM Branch WHERE name=? AND repo_id=?";
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                       get_branch, commit_id,
                                       2, "string", "master", "string", repo_id) < 0) {
        *has_err = TRUE;
        return NULL;
    }

    if (commit_id[0] == 0)
        return NULL;

    SyncwCommit *head_commit = syncw_commit_manager_get_commit (syncw->commit_mgr, repo_id,
                                                              1, commit_id);

    return head_commit;
}

int
syncw_repo_manager_del_repo (SyncwRepoManager *mgr,
                            const char *repo_id,
                            GError **error)
{
    gboolean has_err = FALSE;

    SyncwCommit *head_commit = get_head_commit (mgr, repo_id, &has_err);
    if (has_err) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get head commit from db");
        return -1;
    }
    if (!head_commit) {
        // head commit is missing, del repo directly
        goto del_repo;
    }

    if (add_deleted_repo_to_trash (mgr, repo_id, head_commit) < 0) {
        // Add repo to trash failed, del repo directly
        syncw_warning ("Failed to add repo %.8s to trash, delete directly.\n",
                      repo_id);
    }

    syncw_commit_unref (head_commit);

del_repo:
    if (syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM Repo WHERE repo_id = ?",
                                 1, "string", repo_id) < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to delete repo from db");
        return -1;
    }

    /* remove branch */
    GList *p;
    GList *branch_list = syncw_branch_manager_get_branch_list (syncw->branch_mgr, repo_id);
    for (p = branch_list; p; p = p->next) {
        SyncwBranch *b = (SyncwBranch *)p->data;
        syncw_repo_manager_branch_repo_unmap (mgr, b);
        syncw_branch_manager_del_branch (syncw->branch_mgr, repo_id, b->name);
    }
    syncw_branch_list_free (branch_list);

    syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM RepoOwner WHERE repo_id = ?",
                             1, "string", repo_id);

    syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM SharedRepo WHERE repo_id = ?",
                             1, "string", repo_id);

    syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM RepoGroup WHERE repo_id = ?",
                             1, "string", repo_id);

    if (!syncw->cloud_mode) {
        syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM InnerPubRepo WHERE repo_id = ?",
                                 1, "string", repo_id);
    }

    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoUserToken WHERE repo_id = ?",
                             1, "string", repo_id);

    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoHistoryLimit WHERE repo_id = ?",
                             1, "string", repo_id);

    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoValidSince WHERE repo_id = ?",
                             1, "string", repo_id);

    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoSize WHERE repo_id = ?",
                             1, "string", repo_id);

    /* Remove virtual repos when origin repo is deleted. */
    GList *vrepos, *ptr;
    vrepos = syncw_repo_manager_get_virtual_repo_ids_by_origin (mgr, repo_id);
    for (ptr = vrepos; ptr != NULL; ptr = ptr->next)
        remove_virtual_repo_ondisk (mgr, (char *)ptr->data);
    string_list_free (vrepos);

    syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM VirtualRepo "
                             "WHERE repo_id=? OR origin_repo=?",
                             2, "string", repo_id, "string", repo_id);
    if (!head_commit)
        add_deleted_repo_record(mgr, repo_id);

    return 0;
}

int
syncw_repo_manager_del_virtual_repo (SyncwRepoManager *mgr,
                                    const char *repo_id)
{
    int ret = remove_virtual_repo_ondisk (mgr, repo_id);

    if (ret < 0)
        return ret;

    return syncwerk_server_db_statement_query (mgr->syncw->db,
                                    "DELETE FROM VirtualRepo WHERE repo_id = ?",
                                    1, "string", repo_id);
}

static gboolean
repo_exists_in_db (SyncwDB *db, const char *id, gboolean *db_err)
{
    return syncwerk_server_db_statement_exists (db,
                                     "SELECT repo_id FROM Repo WHERE repo_id = ?",
                                     db_err, 1, "string", id);
}

gboolean
create_repo_fill_size (SyncwDBRow *row, void *data)
{
    SyncwRepo **repo = data;
    SyncwBranch *head;

    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    gint64 size = syncwerk_server_db_row_get_column_int64 (row, 1);
    const char *commit_id = syncwerk_server_db_row_get_column_text (row, 2);
    const char *vrepo_id = syncwerk_server_db_row_get_column_text (row, 3);
    gint64 file_count = syncwerk_server_db_row_get_column_int64 (row, 7);

    *repo = syncw_repo_new (repo_id, NULL, NULL);
    if (!*repo)
        return FALSE;

    if (!commit_id) {
        (*repo)->is_corrupted = TRUE;
        return FALSE;
    }

    (*repo)->size = size;
    (*repo)->file_count = file_count;
    head = syncw_branch_new ("master", repo_id, commit_id);
    (*repo)->head = head;

    if (vrepo_id) {
        const char *origin_repo_id = syncwerk_server_db_row_get_column_text (row, 4);
        const char *origin_path = syncwerk_server_db_row_get_column_text (row, 5);
        const char *base_commit = syncwerk_server_db_row_get_column_text (row, 6);

        SyncwVirtRepo *vinfo = g_new0 (SyncwVirtRepo, 1);
        memcpy (vinfo->repo_id, vrepo_id, 36);
        memcpy (vinfo->origin_repo_id, origin_repo_id, 36);
        vinfo->path = g_strdup(origin_path);
        memcpy (vinfo->base_commit, base_commit, 40);

        (*repo)->virtual_info = vinfo;
        memcpy ((*repo)->store_id, origin_repo_id, 36);
    } else {
        memcpy ((*repo)->store_id, repo_id, 36);
    }

    return TRUE;
}

static SyncwRepo*
get_repo_from_db (SyncwRepoManager *mgr, const char *id, gboolean *db_err)
{
    SyncwRepo *repo = NULL;
    const char *sql;

    if (syncwerk_server_db_type(mgr->syncw->db) != SYNCW_DB_TYPE_PGSQL)
        sql = "SELECT r.repo_id, s.size, b.commit_id, "
            "v.repo_id, v.origin_repo, v.path, v.base_commit, fc.file_count FROM "
            "Repo r LEFT JOIN Branch b ON r.repo_id = b.repo_id "
            "LEFT JOIN RepoSize s ON r.repo_id = s.repo_id "
            "LEFT JOIN VirtualRepo v ON r.repo_id = v.repo_id "
            "LEFT JOIN RepoFileCount fc ON r.repo_id = fc.repo_id "
            "WHERE r.repo_id = ? AND b.name = 'master'";
    else
        sql = "SELECT r.repo_id, s.\"size\", b.commit_id, "
            "v.repo_id, v.origin_repo, v.path, v.base_commit, fc.file_count FROM "
            "Repo r LEFT JOIN Branch b ON r.repo_id = b.repo_id "
            "LEFT JOIN RepoSize s ON r.repo_id = s.repo_id "
            "LEFT JOIN VirtualRepo v ON r.repo_id = v.repo_id "
            "LEFT JOIN RepoFileCount fc ON r.repo_id = fc.repo_id "
            "WHERE r.repo_id = ? AND b.name = 'master'";

    int ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                             create_repo_fill_size, &repo,
                                             1, "string", id);
    if (ret < 0)
        *db_err = TRUE;

    return repo;
}

SyncwRepo*
syncw_repo_manager_get_repo (SyncwRepoManager *manager, const gchar *id)
{
    int len = strlen(id);
    SyncwRepo *repo = NULL;
    gboolean has_err = FALSE;

    if (len >= 37)
        return NULL;

    repo = get_repo_from_db (manager, id, &has_err);

    if (repo) {
        if (repo->is_corrupted) {
            syncw_repo_unref (repo);
            return NULL;
        }

        load_repo (manager, repo);
        if (repo->is_corrupted) {
            syncw_repo_unref (repo);
            return NULL;
        }
    }

    return repo;
}

SyncwRepo*
syncw_repo_manager_get_repo_ex (SyncwRepoManager *manager, const gchar *id)
{
    int len = strlen(id);
    gboolean has_err = FALSE;
    SyncwRepo *ret = NULL;

    if (len >= 37)
        return NULL;

    ret = get_repo_from_db (manager, id, &has_err);
    if (has_err) {
        ret = syncw_repo_new(id, NULL, NULL);
        ret->is_corrupted = TRUE;
        return ret;
    }

    if (ret) {
        if (ret->is_corrupted) {
            return ret;
        }

        load_repo (manager, ret);
    }

    return ret;
}

gboolean
syncw_repo_manager_repo_exists (SyncwRepoManager *manager, const gchar *id)
{
    gboolean db_err = FALSE;
    return repo_exists_in_db (manager->syncw->db, id, &db_err);
}

static int
save_branch_repo_map (SyncwRepoManager *manager, SyncwBranch *branch)
{
    if (syncwerk_server_db_type(syncw->db) == SYNCW_DB_TYPE_PGSQL) {
        gboolean exists, err;
        int rc;

        exists = syncwerk_server_db_statement_exists (syncw->db,
                                           "SELECT repo_id FROM RepoHead WHERE repo_id=?",
                                           &err, 1, "string", branch->repo_id);
        if (err)
            return -1;

        if (exists)
            rc = syncwerk_server_db_statement_query (syncw->db,
                                          "UPDATE RepoHead SET branch_name=? "
                                          "WHERE repo_id=?",
                                          2, "string", branch->name,
                                          "string", branch->repo_id);
        else
            rc = syncwerk_server_db_statement_query (syncw->db,
                                          "INSERT INTO RepoHead (repo_id, branch_name) VALUES (?, ?)",
                                          2, "string", branch->repo_id,
                                          "string", branch->name);
        return rc;
    } else {
        return syncwerk_server_db_statement_query (syncw->db,
                                        "REPLACE INTO RepoHead (repo_id, branch_name) VALUES (?, ?)",
                                        2, "string", branch->repo_id,
                                        "string", branch->name);
    }

    return -1;
}

int
syncw_repo_manager_branch_repo_unmap (SyncwRepoManager *manager, SyncwBranch *branch)
{
    return syncwerk_server_db_statement_query (syncw->db,
                                    "DELETE FROM RepoHead WHERE branch_name = ?"
                                    " AND repo_id = ?",
                                    2, "string", branch->name,
                                    "string", branch->repo_id);
}

int
set_repo_commit_to_db (const char *repo_id, const char *repo_name, gint64 update_time,
                       int version, gboolean is_encrypted, const char *last_modifier)
{
    int db_type = syncwerk_server_db_type (syncw->db);
    char *sql;
    gboolean exists = FALSE, db_err = FALSE;

    if (db_type == SYNCW_DB_TYPE_SQLITE || db_type == SYNCW_DB_TYPE_MYSQL) {
        sql = "REPLACE INTO RepoInfo (repo_id, name, update_time, version, is_encrypted, last_modifier) "
            "VALUES (?, ?, ?, ?, ?, ?)";
        if (syncwerk_server_db_statement_query (syncw->db, sql, 6,
                                     "string", repo_id,
                                     "string", repo_name,
                                     "int64", update_time,
                                     "int", version,
                                     "int", (is_encrypted ? 1:0),
                                     "string", last_modifier) < 0) { 
            syncw_warning ("Failed to add repo info for repo %s.\n", repo_id);
            return -1;
        }    
    } else {
        sql = "SELECT 1 FROM RepoInfo WHERE repo_id=?";
        exists = syncwerk_server_db_statement_exists (syncw->db, sql, &db_err, 1, "string", repo_id);
        if (db_err)
            return -1;

        if (exists) {
            sql = "UPDATE RepoInfo SET name=?, update_time=?, version=?, is_encrypted=?, "
                "last_modifier=? WHERE repo_id=?";
            if (syncwerk_server_db_statement_query (syncw->db, sql, 6,
                                         "string", repo_name,
                                         "int64", update_time,
                                         "int", version,
                                         "int", (is_encrypted ? 1:0),
                                         "string", last_modifier,
                                         "string", repo_id) < 0) { 
                syncw_warning ("Failed to update repo info for repo %s.\n", repo_id);
                return -1;
            }    
        } else {
            sql = "INSERT INTO RepoInfo (repo_id, name, update_time, version, is_encrypted, last_modifier) "
                "VALUES (?, ?, ?, ?, ?, ?)";
            if (syncwerk_server_db_statement_query (syncw->db, sql, 6,
                                         "string", repo_id,
                                         "string", repo_name,
                                         "int64", update_time,
                                         "int", version,
                                         "int", (is_encrypted ? 1:0),
                                         "string", last_modifier) < 0) {
                syncw_warning ("Failed to add repo info for repo %s.\n", repo_id);
                return -1;
            }
        }
    }

    return 0;
}

static void
load_repo_commit (SyncwRepoManager *manager,
                  SyncwRepo *repo)
{
    SyncwCommit *commit;

    commit = syncw_commit_manager_get_commit_compatible (manager->syncw->commit_mgr,
                                                        repo->id,
                                                        repo->head->commit_id);
    if (!commit) {
        syncw_warning ("Commit %s:%s is missing\n", repo->id, repo->head->commit_id);
        repo->is_corrupted = TRUE;
        return;
    }

    syncw_repo_from_commit (repo, commit);

    syncw_commit_unref (commit);
}

static void
load_repo (SyncwRepoManager *manager, SyncwRepo *repo)
{
    repo->manager = manager;

    load_repo_commit (manager, repo);
}

static void
load_mini_repo (SyncwRepoManager *manager, SyncwRepo *repo)
{
    repo->manager = manager;
    SyncwCommit *commit;

    commit = syncw_commit_manager_get_commit_compatible (manager->syncw->commit_mgr,
                                                        repo->id,
                                                        repo->head->commit_id);
    if (!commit) {
        syncw_warning ("Commit %s:%s is missing\n", repo->id, repo->head->commit_id);
        repo->is_corrupted = TRUE;
        return;
    }

    repo->name = g_strdup (commit->repo_name);
    repo->encrypted = commit->encrypted;
    repo->last_modify = commit->ctime;
    repo->version = commit->version;
    repo->last_modifier = g_strdup (commit->creator_name);

    syncw_commit_unref (commit);
}

static int
create_tables_mysql (SyncwRepoManager *mgr)
{
    SyncwDB *db = mgr->syncw->db;
    char *sql;

    sql = "CREATE TABLE IF NOT EXISTS Repo (id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
          "repo_id CHAR(37), UNIQUE INDEX (repo_id))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoOwner ("
        "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(37), "
        "owner_id VARCHAR(255),"
        "UNIQUE INDEX (repo_id), INDEX (owner_id))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoGroup (id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT,"
        "repo_id CHAR(37), "
        "group_id INTEGER, user_name VARCHAR(255), permission CHAR(15), "
        "UNIQUE INDEX (group_id, repo_id), "
        "INDEX (repo_id), INDEX (user_name))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS InnerPubRepo ("
        "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(37),"
        "permission CHAR(15), UNIQUE INDEX (repo_id))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoUserToken ("
        "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(37), "
        "email VARCHAR(255), "
        "token CHAR(41), "
        "UNIQUE INDEX (repo_id, token), INDEX (email))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoTokenPeerInfo ("
        "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "token CHAR(41), "
        "peer_id CHAR(41), "
        "peer_ip VARCHAR(41), "
        "peer_name VARCHAR(255), "
        "sync_time BIGINT, "
        "client_ver VARCHAR(20), UNIQUE INDEX(token))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoHead ("
        "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(37), branch_name VARCHAR(10), UNIQUE INDEX(repo_id))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoSize ("
        "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(37),"
        "size BIGINT UNSIGNED,"
        "head_id CHAR(41), UNIQUE INDEX (repo_id))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoHistoryLimit ("
        "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(37), days INTEGER, UNIQUE INDEX(repo_id))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoValidSince ("
        "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(37), timestamp BIGINT, UNIQUE INDEX(repo_id))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS WebAP (id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(37), "
        "access_property CHAR(10), UNIQUE INDEX(repo_id))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS VirtualRepo (id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(36),"
        "origin_repo CHAR(36), path TEXT, base_commit CHAR(40), UNIQUE INDEX(repo_id), INDEX(origin_repo))"
        "ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS GarbageRepos (id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
          "repo_id CHAR(36), UNIQUE INDEX(repo_id))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoTrash (id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(36),"
        "repo_name VARCHAR(255), head_id CHAR(40), owner_id VARCHAR(255),"
        "size BIGINT(20), org_id INTEGER, del_time BIGINT, "
        "UNIQUE INDEX(repo_id), INDEX(owner_id), INDEX(org_id))ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoFileCount ("
        "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(36),"
        "file_count BIGINT UNSIGNED, UNIQUE INDEX(repo_id))ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoInfo (id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
        "repo_id CHAR(36), "
        "name VARCHAR(255) NOT NULL, update_time BIGINT, version INTEGER, "
        "is_encrypted INTEGER, last_modifier VARCHAR(255), UNIQUE INDEX(repo_id)) ENGINE=INNODB";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    return 0;
}

static int
create_tables_sqlite (SyncwRepoManager *mgr)
{
    SyncwDB *db = mgr->syncw->db;
    char *sql;

    sql = "CREATE TABLE IF NOT EXISTS Repo (repo_id CHAR(37) PRIMARY KEY)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    /* Owner */

    sql = "CREATE TABLE IF NOT EXISTS RepoOwner ("
        "repo_id CHAR(37) PRIMARY KEY, "
        "owner_id TEXT)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;
    sql = "CREATE INDEX IF NOT EXISTS OwnerIndex ON RepoOwner (owner_id)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    /* Group repo */

    sql = "CREATE TABLE IF NOT EXISTS RepoGroup (repo_id CHAR(37), "
        "group_id INTEGER, user_name TEXT, permission CHAR(15))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE UNIQUE INDEX IF NOT EXISTS groupid_repoid_indx on "
        "RepoGroup (group_id, repo_id)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE INDEX IF NOT EXISTS repogroup_repoid_index on "
        "RepoGroup (repo_id)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE INDEX IF NOT EXISTS repogroup_username_indx on "
        "RepoGroup (user_name)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    /* Public repo */

    sql = "CREATE TABLE IF NOT EXISTS InnerPubRepo ("
        "repo_id CHAR(37) PRIMARY KEY,"
        "permission CHAR(15))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoUserToken ("
        "repo_id CHAR(37), "
        "email VARCHAR(255), "
        "token CHAR(41))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE UNIQUE INDEX IF NOT EXISTS repo_token_indx on "
        "RepoUserToken (repo_id, token)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE INDEX IF NOT EXISTS repo_token_email_indx on "
        "RepoUserToken (email)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoTokenPeerInfo ("
        "token CHAR(41) PRIMARY KEY, "
        "peer_id CHAR(41), "
        "peer_ip VARCHAR(41), "
        "peer_name VARCHAR(255), "
        "sync_time BIGINT, "
        "client_ver VARCHAR(20))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoHead ("
        "repo_id CHAR(37) PRIMARY KEY, branch_name VARCHAR(10))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoSize ("
        "repo_id CHAR(37) PRIMARY KEY,"
        "size BIGINT UNSIGNED,"
        "head_id CHAR(41))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoHistoryLimit ("
        "repo_id CHAR(37) PRIMARY KEY, days INTEGER)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoValidSince ("
        "repo_id CHAR(37) PRIMARY KEY, timestamp BIGINT)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS WebAP (repo_id CHAR(37) PRIMARY KEY, "
        "access_property CHAR(10))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS VirtualRepo (repo_id CHAR(36) PRIMARY KEY,"
        "origin_repo CHAR(36), path TEXT, base_commit CHAR(40))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE INDEX IF NOT EXISTS virtualrepo_origin_repo_idx "
        "ON VirtualRepo (origin_repo)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS GarbageRepos (repo_id CHAR(36) PRIMARY KEY)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoTrash (repo_id CHAR(36) PRIMARY KEY,"
        "repo_name VARCHAR(255), head_id CHAR(40), owner_id VARCHAR(255), size BIGINT UNSIGNED,"
        "org_id INTEGER, del_time BIGINT)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE INDEX IF NOT EXISTS repotrash_owner_id_idx ON RepoTrash(owner_id)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE INDEX IF NOT EXISTS repotrash_org_id_idx ON RepoTrash(org_id)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoFileCount ("
        "repo_id CHAR(36) PRIMARY KEY,"
        "file_count BIGINT UNSIGNED)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoInfo (repo_id CHAR(36) PRIMARY KEY, "
        "name VARCHAR(255) NOT NULL, update_time INTEGER, version INTEGER, "
        "is_encrypted INTEGER, last_modifier VARCHAR(255))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    return 0;
}

static int
create_tables_pgsql (SyncwRepoManager *mgr)
{
    SyncwDB *db = mgr->syncw->db;
    char *sql;

    sql = "CREATE TABLE IF NOT EXISTS Repo (repo_id CHAR(36) PRIMARY KEY)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoOwner ("
        "repo_id CHAR(36) PRIMARY KEY, "
        "owner_id VARCHAR(255))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    if (!pgsql_index_exists (db, "repoowner_owner_idx")) {
        sql = "CREATE INDEX repoowner_owner_idx ON RepoOwner (owner_id)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
    }

    sql = "CREATE TABLE IF NOT EXISTS RepoGroup (repo_id CHAR(36), "
        "group_id INTEGER, user_name VARCHAR(255), permission VARCHAR(15), "
        "UNIQUE (group_id, repo_id))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    if (!pgsql_index_exists (db, "repogroup_repoid_idx")) {
        sql = "CREATE INDEX repogroup_repoid_idx ON RepoGroup (repo_id)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
    }

    if (!pgsql_index_exists (db, "repogroup_username_idx")) {
        sql = "CREATE INDEX repogroup_username_idx ON RepoGroup (user_name)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
    }

    sql = "CREATE TABLE IF NOT EXISTS InnerPubRepo ("
        "repo_id CHAR(36) PRIMARY KEY,"
        "permission VARCHAR(15))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoUserToken ("
        "repo_id CHAR(36), "
        "email VARCHAR(255), "
        "token CHAR(40), "
        "UNIQUE (repo_id, token))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    if (!pgsql_index_exists (db, "repousertoken_email_idx")) {
        sql = "CREATE INDEX repousertoken_email_idx ON RepoUserToken (email)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
    }

    sql = "CREATE TABLE IF NOT EXISTS RepoTokenPeerInfo ("
        "token CHAR(40) PRIMARY KEY, "
        "peer_id CHAR(40), "
        "peer_ip VARCHAR(40), "
        "peer_name VARCHAR(255), "
        "sync_time BIGINT, "
        "client_ver VARCHAR(20))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoHead ("
        "repo_id CHAR(36) PRIMARY KEY, branch_name VARCHAR(10))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoSize ("
        "repo_id CHAR(36) PRIMARY KEY,"
        "size BIGINT,"
        "head_id CHAR(40))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoHistoryLimit ("
        "repo_id CHAR(36) PRIMARY KEY, days INTEGER)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoValidSince ("
        "repo_id CHAR(36) PRIMARY KEY, timestamp BIGINT)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS WebAP (repo_id CHAR(36) PRIMARY KEY, "
        "access_property VARCHAR(10))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS VirtualRepo (repo_id CHAR(36) PRIMARY KEY,"
        "origin_repo CHAR(36), path TEXT, base_commit CHAR(40))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    if (!pgsql_index_exists (db, "virtualrepo_origin_repo_idx")) {
        sql = "CREATE INDEX virtualrepo_origin_repo_idx ON VirtualRepo (origin_repo)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
    }

    sql = "CREATE TABLE IF NOT EXISTS GarbageRepos (repo_id CHAR(36) PRIMARY KEY)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoTrash (repo_id CHAR(36) PRIMARY KEY,"
        "repo_name VARCHAR(255), head_id CHAR(40), owner_id VARCHAR(255), size bigint,"
        "org_id INTEGER, del_time BIGINT)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    if (!pgsql_index_exists (db, "repotrash_owner_id")) {
        sql = "CREATE INDEX repotrash_owner_id on RepoTrash(owner_id)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
    }
    if (!pgsql_index_exists (db, "repotrash_org_id")) {
        sql = "CREATE INDEX repotrash_org_id on RepoTrash(org_id)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
    }

    sql = "CREATE TABLE IF NOT EXISTS RepoFileCount ("
        "repo_id CHAR(36) PRIMARY KEY,"
        "file_count BIGINT)";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    sql = "CREATE TABLE IF NOT EXISTS RepoInfo (repo_id CHAR(36) PRIMARY KEY, "
        "name VARCHAR(255) NOT NULL, update_time BIGINT, version INTEGER, "
        "is_encrypted INTEGER, last_modifier VARCHAR(255))";
    if (syncwerk_server_db_query (db, sql) < 0)
        return -1;

    return 0;
}

static int
create_db_tables_if_not_exist (SyncwRepoManager *mgr)
{
    if (!mgr->syncw->create_tables && syncwerk_server_db_type (mgr->syncw->db) == SYNCW_DB_TYPE_MYSQL)
        return 0;

    SyncwDB *db = mgr->syncw->db;
    int db_type = syncwerk_server_db_type (db);

    if (db_type == SYNCW_DB_TYPE_MYSQL)
        return create_tables_mysql (mgr);
    else if (db_type == SYNCW_DB_TYPE_SQLITE)
        return create_tables_sqlite (mgr);
    else if (db_type == SYNCW_DB_TYPE_PGSQL)
        return create_tables_pgsql (mgr);

    g_return_val_if_reached (-1);
}

/*
 * Repo properties functions.
 */

static inline char *
generate_repo_token ()
{
    char *uuid = gen_uuid ();
    unsigned char sha1[20];
    char token[41];
    SHA_CTX s;

    SHA1_Init (&s);
    SHA1_Update (&s, uuid, strlen(uuid));
    SHA1_Final (sha1, &s);

    rawdata_to_hex (sha1, token, 20);

    g_free (uuid);

    return g_strdup (token);
}

static int
add_repo_token (SyncwRepoManager *mgr,
                const char *repo_id,
                const char *email,
                const char *token,
                GError **error)
{
    int rc = syncwerk_server_db_statement_query (mgr->syncw->db,
                                      "INSERT INTO RepoUserToken (repo_id, email, token) VALUES (?, ?, ?)",
                                      3, "string", repo_id, "string", email,
                                      "string", token);

    if (rc < 0) {
        syncw_warning ("failed to add repo token. repo = %s, email = %s\n",
                      repo_id, email);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "DB error");
        return -1;
    }

    return 0;
}

char *
syncw_repo_manager_generate_repo_token (SyncwRepoManager *mgr,
                                       const char *repo_id,
                                       const char *email,
                                       GError **error)
{
    char *token = generate_repo_token ();
    if (add_repo_token (mgr, repo_id, email, token, error) < 0) {
        g_free (token);        
        return NULL;
    }

    return token;
}

int
syncw_repo_manager_add_token_peer_info (SyncwRepoManager *mgr,
                                       const char *token,
                                       const char *peer_id,
                                       const char *peer_ip,
                                       const char *peer_name,
                                       gint64 sync_time,
                                       const char *client_ver)
{
    int ret = 0;

    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                                 "INSERT INTO RepoTokenPeerInfo (token, peer_id, peer_ip, peer_name, sync_time, client_ver)"
                                 "VALUES (?, ?, ?, ?, ?, ?)",
                                 6, "string", token,
                                 "string", peer_id,
                                 "string", peer_ip,
                                 "string", peer_name,
                                 "int64", sync_time,
                                 "string", client_ver) < 0)
        ret = -1;

    return ret;
}

int
syncw_repo_manager_update_token_peer_info (SyncwRepoManager *mgr,
                                          const char *token,
                                          const char *peer_ip,
                                          gint64 sync_time,
                                          const char *client_ver)
{
    int ret = 0;

    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                                 "UPDATE RepoTokenPeerInfo SET "
                                 "peer_ip=?, sync_time=?, client_ver=? WHERE token=?",
                                 4, "string", peer_ip,
                                 "int64", sync_time,
                                 "string", client_ver,
                                 "string", token) < 0)
        ret = -1;

    return ret;
}

gboolean
syncw_repo_manager_token_peer_info_exists (SyncwRepoManager *mgr,
                                          const char *token)
{
    gboolean db_error = FALSE;

    return syncwerk_server_db_statement_exists (mgr->syncw->db,
                                     "SELECT token FROM RepoTokenPeerInfo WHERE token=?",
                                     &db_error, 1, "string", token);
}

int
syncw_repo_manager_delete_token (SyncwRepoManager *mgr,
                                const char *repo_id,
                                const char *token,
                                const char *user,
                                GError **error)
{
    char *token_owner;

    token_owner = syncw_repo_manager_get_email_by_token (mgr, repo_id, token);
    if (!token_owner || strcmp (user, token_owner) != 0) {
        syncw_warning ("Requesting user is %s, token owner is %s, "
                      "refuse to delete token %.10s.\n", user, token_owner, token);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Permission denied");
        return -1;
    }

    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                                 "DELETE FROM RepoUserToken "
                                 "WHERE repo_id=? and token=?",
                                 2, "string", repo_id, "string", token) < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "DB error");
        return -1;
    }

    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                                 "DELETE FROM RepoTokenPeerInfo WHERE token=?",
                                 1, "string", token) < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "DB error");
        return -1;
    }

    GList *tokens = NULL;
    tokens = g_list_append (tokens, g_strdup(token));
    syncw_http_server_invalidate_tokens (syncw->http_server, tokens);
    g_list_free_full (tokens, (GDestroyNotify)g_free);

    return 0;
}

static gboolean
collect_repo_token (SyncwDBRow *row, void *data)
{
    GList **ret_list = data;
    const char *repo_id, *repo_owner, *email, *token;
    const char *peer_id, *peer_ip, *peer_name;
    gint64 sync_time;
    const char *client_ver;

    repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    repo_owner = syncwerk_server_db_row_get_column_text (row, 1);
    email = syncwerk_server_db_row_get_column_text (row, 2);
    token = syncwerk_server_db_row_get_column_text (row, 3);

    peer_id = syncwerk_server_db_row_get_column_text (row, 4);
    peer_ip = syncwerk_server_db_row_get_column_text (row, 5);
    peer_name = syncwerk_server_db_row_get_column_text (row, 6);
    sync_time = syncwerk_server_db_row_get_column_int64 (row, 7);
    client_ver = syncwerk_server_db_row_get_column_text (row, 8);

    char *owner_l = g_ascii_strdown (repo_owner, -1);
    char *email_l = g_ascii_strdown (email, -1);

    SyncwerkRepoTokenInfo *repo_token_info;
    repo_token_info = g_object_new (SYNCWERK_TYPE_REPO_TOKEN_INFO,
                                    "repo_id", repo_id,
                                    "repo_owner", owner_l,
                                    "email", email_l,
                                    "token", token,
                                    "peer_id", peer_id,
                                    "peer_ip", peer_ip,
                                    "peer_name", peer_name,
                                    "sync_time", sync_time,
                                    "client_ver", client_ver,
                                    NULL);

    *ret_list = g_list_prepend (*ret_list, repo_token_info);

    g_free (owner_l);
    g_free (email_l);

    return TRUE;
}

static void
fill_in_token_info (GList *info_list)
{
    GList *ptr;
    SyncwerkRepoTokenInfo *info;
    SyncwRepo *repo;
    char *repo_name;

    for (ptr = info_list; ptr; ptr = ptr->next) {
        info = ptr->data;
        repo = syncw_repo_manager_get_repo (syncw->repo_mgr,
                                           syncwerk_repo_token_info_get_repo_id(info));
        if (repo)
            repo_name = g_strdup(repo->name);
        else
            repo_name = g_strdup("Unknown");
        syncw_repo_unref (repo);

        g_object_set (info, "repo_name", repo_name, NULL);
        g_free (repo_name);
    }
}

GList *
syncw_repo_manager_list_repo_tokens (SyncwRepoManager *mgr,
                                    const char *repo_id,
                                    GError **error)
{
    GList *ret_list = NULL;
    char *sql;
    gboolean db_err = FALSE;

    if (!repo_exists_in_db (mgr->syncw->db, repo_id, &db_err)) {
        if (db_err) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "DB error");
        }
        return NULL;
    }

    sql = "SELECT u.repo_id, o.owner_id, u.email, u.token, "
        "p.peer_id, p.peer_ip, p.peer_name, p.sync_time, p.client_ver "
        "FROM RepoUserToken u LEFT JOIN RepoTokenPeerInfo p "
        "ON u.token = p.token, RepoOwner o "
        "WHERE u.repo_id = ? and o.repo_id = ? ";

    int n_row = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                              collect_repo_token, &ret_list,
                                              2, "string", repo_id,
                                              "string", repo_id);
    if (n_row < 0) {
        syncw_warning ("DB error when get token info for repo %.10s.\n",
                      repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "DB error");
    }

    fill_in_token_info (ret_list);

    return g_list_reverse(ret_list);
}

GList *
syncw_repo_manager_list_repo_tokens_by_email (SyncwRepoManager *mgr,
                                             const char *email,
                                             GError **error)
{
    GList *ret_list = NULL;
    char *sql;

    sql = "SELECT u.repo_id, o.owner_id, u.email, u.token, "
        "p.peer_id, p.peer_ip, p.peer_name, p.sync_time, p.client_ver "
        "FROM RepoUserToken u LEFT JOIN RepoTokenPeerInfo p "
        "ON u.token = p.token, RepoOwner o "
        "WHERE u.email = ? and u.repo_id = o.repo_id";

    int n_row = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                              collect_repo_token, &ret_list,
                                              1, "string", email);
    if (n_row < 0) {
        syncw_warning ("DB error when get token info for email %s.\n",
                      email);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "DB error");
    }

    fill_in_token_info (ret_list);

    return g_list_reverse(ret_list);
}

static gboolean
collect_token_list (SyncwDBRow *row, void *data)
{
    GList **p_tokens = data;
    const char *token;

    token = syncwerk_server_db_row_get_column_text (row, 0);
    *p_tokens = g_list_prepend (*p_tokens, g_strdup(token));

    return TRUE;
}

/**
 * Delete all repo tokens for a given user on a given client
 */

int
syncw_repo_manager_delete_repo_tokens_by_peer_id (SyncwRepoManager *mgr,
                                                 const char *email,
                                                 const char *peer_id,
                                                 GList **tokens,
                                                 GError **error)
{
    int ret = 0;
    const char *template;
    GList *token_list = NULL;
    GString *token_list_str = g_string_new ("");
    GString *sql = g_string_new ("");
    GList *ptr;
    int rc = 0;

    template = "SELECT u.token "
        "FROM RepoUserToken u, RepoTokenPeerInfo p "
        "WHERE u.token = p.token "
        "AND u.email = ? AND p.peer_id = ?";
    rc = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, template,
                                        collect_token_list, &token_list,
                                        2, "string", email, "string", peer_id);
    if (rc < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "DB error");
        goto out;
    }

    if (rc == 0)
        goto out;

    for (ptr = token_list; ptr; ptr = ptr->next) {
        const char *token = (char *)ptr->data;
        if (token_list_str->len == 0)
            g_string_append_printf (token_list_str, "'%s'", token);
        else
            g_string_append_printf (token_list_str, ",'%s'", token);
    }

    /* Note that there is a size limit on sql query. In MySQL it's 1MB by default.
     * Normally the token_list won't be that long.
     */
    g_string_printf (sql, "DELETE FROM RepoUserToken WHERE token in (%s)",
                     token_list_str->str);
    rc = syncwerk_server_db_statement_query (mgr->syncw->db, sql->str, 0);
    if (rc < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "DB error");
        goto out;
    }

    g_string_printf (sql, "DELETE FROM RepoTokenPeerInfo WHERE token in (%s)",
                     token_list_str->str);
    rc = syncwerk_server_db_statement_query (mgr->syncw->db, sql->str, 0);
    if (rc < 0)
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "DB error");

out:
    g_string_free (token_list_str, TRUE);
    g_string_free (sql, TRUE);

    if (rc < 0) {
        ret = -1;
        g_list_free_full (token_list, (GDestroyNotify)g_free);
    } else {
        *tokens = token_list;
    }

    return ret;
}

int
syncw_repo_manager_delete_repo_tokens_by_email (SyncwRepoManager *mgr,
                                               const char *email,
                                               GError **error)
{
    int ret = 0;
    const char *template;
    GList *token_list = NULL;
    GList *ptr;
    GString *token_list_str = g_string_new ("");
    GString *sql = g_string_new ("");
    int rc;

    template = "SELECT u.token "
        "FROM RepoUserToken u, RepoTokenPeerInfo p "
        "WHERE u.token = p.token "
        "AND u.email = ?";
    rc = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, template,
                                        collect_token_list, &token_list,
                                        1, "string", email);
    if (rc < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "DB error");
        goto out;
    }

    if (rc == 0)
        goto out;

    for (ptr = token_list; ptr; ptr = ptr->next) {
        const char *token = (char *)ptr->data;
        if (token_list_str->len == 0)
            g_string_append_printf (token_list_str, "'%s'", token);
        else
            g_string_append_printf (token_list_str, ",'%s'", token);
    }

    /* Note that there is a size limit on sql query. In MySQL it's 1MB by default.
     * Normally the token_list won't be that long.
     */
    g_string_printf (sql, "DELETE FROM RepoUserToken WHERE token in (%s)",
                     token_list_str->str);
    rc = syncwerk_server_db_statement_query (mgr->syncw->db, sql->str, 0);
    if (rc < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "DB error");
        goto out;
    }

    g_string_printf (sql, "DELETE FROM RepoTokenPeerInfo WHERE token in (%s)",
                     token_list_str->str);
    rc = syncwerk_server_db_statement_query (mgr->syncw->db, sql->str, 0);
    if (rc < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "DB error");
        goto out;
    }

    syncw_http_server_invalidate_tokens (syncw->http_server, token_list);

out:
    g_string_free (token_list_str, TRUE);
    g_string_free (sql, TRUE);
    g_list_free_full (token_list, (GDestroyNotify)g_free);

    if (rc < 0) {
        ret = -1;
    }

    return ret;
}

static gboolean
get_email_by_token_cb (SyncwDBRow *row, void *data)
{
    char **email_ptr = data;

    const char *email = (const char *) syncwerk_server_db_row_get_column_text (row, 0);
    *email_ptr = g_ascii_strdown (email, -1);
    /* There should be only one result. */
    return FALSE;
}

char *
syncw_repo_manager_get_email_by_token (SyncwRepoManager *manager,
                                      const char *repo_id,
                                      const char *token)
{
    if (!repo_id || !token)
        return NULL;
    
    char *email = NULL;
    char *sql;

    sql = "SELECT email FROM RepoUserToken "
        "WHERE repo_id = ? AND token = ?";

    syncwerk_server_db_statement_foreach_row (syncw->db, sql,
                                   get_email_by_token_cb, &email,
                                   2, "string", repo_id, "string", token);

    return email;
}

static gboolean
get_repo_size (SyncwDBRow *row, void *vsize)
{
    gint64 *psize = vsize;

    *psize = syncwerk_server_db_row_get_column_int64 (row, 0);

    return FALSE;
}

gint64
syncw_repo_manager_get_repo_size (SyncwRepoManager *mgr, const char *repo_id)
{
    gint64 size = 0;
    char *sql;

    sql = "SELECT size FROM RepoSize WHERE repo_id=?";

    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                       get_repo_size, &size,
                                       1, "string", repo_id) < 0)
        return -1;

    return size;
}

int
syncw_repo_manager_set_repo_history_limit (SyncwRepoManager *mgr,
                                          const char *repo_id,
                                          int days)
{
    SyncwVirtRepo *vinfo;
    SyncwDB *db = mgr->syncw->db;

    vinfo = syncw_repo_manager_get_virtual_repo_info (mgr, repo_id);
    if (vinfo) {
        syncw_virtual_repo_info_free (vinfo);
        return 0;
    }

    if (syncwerk_server_db_type(db) == SYNCW_DB_TYPE_PGSQL) {
        gboolean exists, err;
        int rc;

        exists = syncwerk_server_db_statement_exists (db,
                                           "SELECT repo_id FROM RepoHistoryLimit "
                                           "WHERE repo_id=?",
                                           &err, 1, "string", repo_id);
        if (err)
            return -1;

        if (exists)
            rc = syncwerk_server_db_statement_query (db,
                                          "UPDATE RepoHistoryLimit SET days=? "
                                          "WHERE repo_id=?",
                                          2, "int", days, "string", repo_id);
        else
            rc = syncwerk_server_db_statement_query (db,
                                          "INSERT INTO RepoHistoryLimit (repo_id, days) VALUES "
                                          "(?, ?)",
                                          2, "string", repo_id, "int", days);
        return rc;
    } else {
        if (syncwerk_server_db_statement_query (db,
                                     "REPLACE INTO RepoHistoryLimit (repo_id, days) VALUES (?, ?)",
                                     2, "string", repo_id, "int", days) < 0)
            return -1;
    }

    return 0;
}

static gboolean
get_history_limit_cb (SyncwDBRow *row, void *data)
{
    int *limit = data;

    *limit = syncwerk_server_db_row_get_column_int (row, 0);

    return FALSE;
}

int
syncw_repo_manager_get_repo_history_limit (SyncwRepoManager *mgr,
                                          const char *repo_id)
{
    SyncwVirtRepo *vinfo;
    const char *r_repo_id = repo_id;
    char *sql;
    int per_repo_days = -1;
    int ret;

    vinfo = syncw_repo_manager_get_virtual_repo_info (mgr, repo_id);
    if (vinfo)
        r_repo_id = vinfo->origin_repo_id;

    sql = "SELECT days FROM RepoHistoryLimit WHERE repo_id=?";

    ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, get_history_limit_cb,
                                         &per_repo_days, 1, "string", r_repo_id);
    if (ret == 0) {
        // limit not set, return global one
        per_repo_days= syncw_cfg_manager_get_config_int (mgr->syncw->cfg_mgr,
                                                        "history", "keep_days");
    }

    // db error or limit set as negative, means keep full history, return -1
    if (per_repo_days < 0)
        per_repo_days = -1;

    syncw_virtual_repo_info_free (vinfo);

    return per_repo_days;
}

int
syncw_repo_manager_set_repo_valid_since (SyncwRepoManager *mgr,
                                        const char *repo_id,
                                        gint64 timestamp)
{
    SyncwDB *db = mgr->syncw->db;

    if (syncwerk_server_db_type(db) == SYNCW_DB_TYPE_PGSQL) {
        gboolean exists, err;
        int rc;

        exists = syncwerk_server_db_statement_exists (db,
                                           "SELECT repo_id FROM RepoValidSince WHERE "
                                           "repo_id=?", &err, 1, "string", repo_id);
        if (err)
            return -1;

        if (exists)
            rc = syncwerk_server_db_statement_query (db,
                                          "UPDATE RepoValidSince SET timestamp=?"
                                          " WHERE repo_id=?",
                                          2, "int64", timestamp, "string", repo_id);
        else
            rc = syncwerk_server_db_statement_query (db,
                                          "INSERT INTO RepoValidSince (repo_id, timestamp) VALUES "
                                          "(?, ?)", 2, "string", repo_id,
                                          "int64", timestamp);
        if (rc < 0)
            return -1;
    } else {
        if (syncwerk_server_db_statement_query (db,
                           "REPLACE INTO RepoValidSince (repo_id, timestamp) VALUES (?, ?)",
                           2, "string", repo_id, "int64", timestamp) < 0)
            return -1;
    }

    return 0;
}

gint64
syncw_repo_manager_get_repo_valid_since (SyncwRepoManager *mgr,
                                        const char *repo_id)
{
    char *sql;

    sql = "SELECT timestamp FROM RepoValidSince WHERE repo_id=?";
    /* Also return -1 if doesn't exist. */
    return syncwerk_server_db_statement_get_int64 (mgr->syncw->db, sql, 1, "string", repo_id);
}

gint64
syncw_repo_manager_get_repo_truncate_time (SyncwRepoManager *mgr,
                                          const char *repo_id)
{
    int days;
    gint64 timestamp;

    days = syncw_repo_manager_get_repo_history_limit (mgr, repo_id);
    timestamp = syncw_repo_manager_get_repo_valid_since (mgr, repo_id);

    gint64 now = (gint64)time(NULL);
    if (days > 0)
        return MAX (now - days * 24 * 3600, timestamp);
    else if (days < 0)
        return timestamp;
    else
        return 0;
}

/*
 * Permission related functions.
 */

/* Owner functions. */

int
syncw_repo_manager_set_repo_owner (SyncwRepoManager *mgr,
                                  const char *repo_id,
                                  const char *email)
{
    SyncwDB *db = mgr->syncw->db;
    char sql[256];
    char *orig_owner = NULL;
    int ret = 0;

    orig_owner = syncw_repo_manager_get_repo_owner (mgr, repo_id);
    if (g_strcmp0 (orig_owner, email) == 0)
        goto out;

    if (syncwerk_server_db_type(db) == SYNCW_DB_TYPE_PGSQL) {
        gboolean err;
        snprintf(sql, sizeof(sql),
                 "SELECT repo_id FROM RepoOwner WHERE repo_id=?");
        if (syncwerk_server_db_statement_exists (db, sql, &err,
                                      1, "string", repo_id))
            snprintf(sql, sizeof(sql),
                     "UPDATE RepoOwner SET owner_id='%s' WHERE "
                     "repo_id='%s'", email, repo_id);
        else
            snprintf(sql, sizeof(sql),
                     "INSERT INTO RepoOwner (repo_id, owner_id) VALUES ('%s', '%s')",
                     repo_id, email);
        if (err) {
            ret = -1;
            goto out;
        }

        if (syncwerk_server_db_query (db, sql) < 0) {
            ret = -1;
            goto out;
        }
    } else {
        if (syncwerk_server_db_statement_query (db, "REPLACE INTO RepoOwner (repo_id, owner_id) VALUES (?, ?)",
                                     2, "string", repo_id, "string", email) < 0) {
            ret = -1;
            goto out;
        }
    }

    /* If the repo was newly created, no need to remove share and virtual repos. */
    if (!orig_owner)
        goto out;

    syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM SharedRepo WHERE repo_id = ?",
                             1, "string", repo_id);

    syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM RepoGroup WHERE repo_id = ?",
                             1, "string", repo_id);

    if (!syncw->cloud_mode) {
        syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM InnerPubRepo WHERE repo_id = ?",
                                 1, "string", repo_id);
    }

    /* Remove all tokens except for the new owner. */
    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoUserToken WHERE repo_id = ? AND email != ?",
                             2, "string", repo_id, "string", email);

    /* Remove virtual repos when repo ownership changes. */
    GList *vrepos, *ptr;
    vrepos = syncw_repo_manager_get_virtual_repo_ids_by_origin (mgr, repo_id);
    for (ptr = vrepos; ptr != NULL; ptr = ptr->next)
        remove_virtual_repo_ondisk (mgr, (char *)ptr->data);
    string_list_free (vrepos);

    syncwerk_server_db_statement_query (mgr->syncw->db, "DELETE FROM VirtualRepo "
                             "WHERE repo_id=? OR origin_repo=?",
                             2, "string", repo_id, "string", repo_id);

out:
    g_free (orig_owner);
    return ret;
}

static gboolean
get_owner (SyncwDBRow *row, void *data)
{
    char **owner_id = data;

    const char *owner = (const char *) syncwerk_server_db_row_get_column_text (row, 0);
    *owner_id = g_ascii_strdown (owner, -1);
    /* There should be only one result. */
    return FALSE;
}

char *
syncw_repo_manager_get_repo_owner (SyncwRepoManager *mgr,
                                  const char *repo_id)
{
    char *sql;
    char *ret = NULL;

    sql = "SELECT owner_id FROM RepoOwner WHERE repo_id=?";
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                       get_owner, &ret,
                                       1, "string", repo_id) < 0) {
        syncw_warning ("Failed to get owner id for repo %s.\n", repo_id);
        return NULL;
    }

    return ret;
}

static gboolean
collect_repo_id (SyncwDBRow *row, void *data)
{
    GList **p_ids = data;
    const char *repo_id;

    repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    *p_ids = g_list_prepend (*p_ids, g_strdup(repo_id));

    return TRUE;
}

GList *
syncw_repo_manager_get_orphan_repo_list (SyncwRepoManager *mgr)
{
    GList *id_list = NULL, *ptr;
    GList *ret = NULL;
    char sql[256];

    snprintf (sql, sizeof(sql), "SELECT Repo.repo_id FROM Repo LEFT JOIN "
              "RepoOwner ON Repo.repo_id = RepoOwner.repo_id WHERE "
              "RepoOwner.owner_id is NULL");

    if (syncwerk_server_db_foreach_selected_row (mgr->syncw->db, sql,
                                      collect_repo_id, &id_list) < 0)
        return NULL;

    for (ptr = id_list; ptr; ptr = ptr->next) {
        char *repo_id = ptr->data;
        SyncwRepo *repo = syncw_repo_manager_get_repo (mgr, repo_id);
        if (repo != NULL)
            ret = g_list_prepend (ret, repo);
    }

    string_list_free (id_list);

    return ret;
}

gboolean
collect_repos_fill_size_commit (SyncwDBRow *row, void *data)
{
    GList **prepos = data;
    SyncwRepo *repo;
    SyncwBranch *head;

    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    gint64 size = syncwerk_server_db_row_get_column_int64 (row, 1);
    const char *commit_id = syncwerk_server_db_row_get_column_text (row, 2);
    const char *repo_name = syncwerk_server_db_row_get_column_text (row, 3);
    gint64 update_time = syncwerk_server_db_row_get_column_int64 (row, 4);
    int version = syncwerk_server_db_row_get_column_int (row, 5);
    gboolean is_encrypted = syncwerk_server_db_row_get_column_int (row, 6) ? TRUE : FALSE;
    const char *last_modifier = syncwerk_server_db_row_get_column_text (row, 7);

    repo = syncw_repo_new (repo_id, NULL, NULL);
    if (!repo)
        return TRUE;

    if (!commit_id) {
        repo->is_corrupted = TRUE;
        goto out;
    }

    repo->size = size;
    head = syncw_branch_new ("master", repo_id, commit_id);
    repo->head = head;
    if (repo_name) {
        repo->name = g_strdup (repo_name);
        repo->last_modify = update_time;
        repo->version = version;
        repo->encrypted = is_encrypted;
        repo->last_modifier = g_strdup (last_modifier);
    }

out:
    *prepos = g_list_prepend (*prepos, repo);

    return TRUE;
}

GList *
syncw_repo_manager_get_repos_by_owner (SyncwRepoManager *mgr,
                                      const char *email,
                                      int ret_corrupted,
                                      int start,
                                      int limit)
{
    GList *repo_list = NULL, *ptr;
    GList *ret = NULL;
    char *sql;
    SyncwRepo *repo = NULL;
    int db_type = syncwerk_server_db_type(mgr->syncw->db);

    if (start == -1 && limit == -1) {
        if (db_type != SYNCW_DB_TYPE_PGSQL)
            sql = "SELECT o.repo_id, s.size, b.commit_id, i.name, i.update_time, "
                "i.version, i.is_encrypted, i.last_modifier FROM "
                "RepoOwner o LEFT JOIN RepoSize s ON o.repo_id = s.repo_id "
                "LEFT JOIN Branch b ON o.repo_id = b.repo_id "
                "LEFT JOIN RepoInfo i ON o.repo_id = i.repo_id "
                "WHERE owner_id=? AND "
                "o.repo_id NOT IN (SELECT v.repo_id FROM VirtualRepo v) "
                "ORDER BY i.update_time DESC, o.repo_id";
        else
            sql = "SELECT o.repo_id, s.\"size\", b.commit_id, i.name, i.update_time, "
                "i.version, i.is_encrypted, i.last_modifier FROM "
                "RepoOwner o LEFT JOIN RepoSize s ON o.repo_id = s.repo_id "
                "LEFT JOIN Branch b ON o.repo_id = b.repo_id "
                "LEFT JOIN RepoInfo i ON o.repo_id = i.repo_id "
                "WHERE owner_id=? AND "
                "o.repo_id NOT IN (SELECT v.repo_id FROM VirtualRepo v) "
                "ORDER BY i.update_time DESC, o.repo_id";

        if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, 
                                           collect_repos_fill_size_commit, &repo_list,
                                           1, "string", email) < 0)
            return NULL;
    } else {
        if (db_type != SYNCW_DB_TYPE_PGSQL)
            sql = "SELECT o.repo_id, s.size, b.commit_id, i.name, i.update_time, "
                "i.version, i.is_encrypted, i.last_modifier FROM "
                "RepoOwner o LEFT JOIN RepoSize s ON o.repo_id = s.repo_id "
                "LEFT JOIN Branch b ON o.repo_id = b.repo_id "
                "LEFT JOIN RepoInfo i ON o.repo_id = i.repo_id "
                "WHERE owner_id=? AND "
                "o.repo_id NOT IN (SELECT v.repo_id FROM VirtualRepo v) "
                "ORDER BY i.update_time DESC, o.repo_id "
                "LIMIT ? OFFSET ?";
        else
            sql = "SELECT o.repo_id, s.\"size\", b.commit_id, i.name, i.update_time, "
                "i.version, i.is_encrypted, i.last_modifier FROM "
                "RepoOwner o LEFT JOIN RepoSize s ON o.repo_id = s.repo_id "
                "LEFT JOIN Branch b ON o.repo_id = b.repo_id "
                "LEFT JOIN RepoInfo i ON o.repo_id = i.repo_id "
                "WHERE owner_id=? AND "
                "o.repo_id NOT IN (SELECT v.repo_id FROM VirtualRepo v) "
                "ORDER BY i.update_time DESC, o.repo_id "
                "LIMIT ? OFFSET ?";

        if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, 
                                           collect_repos_fill_size_commit,
                                           &repo_list,
                                           3, "string", email,
                                           "int", limit,
                                           "int", start) < 0) {
            return NULL;
        }
    }

    for (ptr = repo_list; ptr; ptr = ptr->next) {
        repo = ptr->data;
        if (ret_corrupted) {
            if (!repo->is_corrupted && (!repo->name || !repo->last_modifier)) {
                load_mini_repo (mgr, repo);
                if (!repo->is_corrupted)
                    set_repo_commit_to_db (repo->id, repo->name, repo->last_modify,
                                           repo->version, (repo->encrypted ? 1 : 0),
                                           repo->last_modifier);
            }
        } else {
            if (repo->is_corrupted) {
                syncw_repo_unref (repo);
                continue;
            }
            if (!repo->name || !repo->last_modifier) {
                load_mini_repo (mgr, repo);
                if (!repo->is_corrupted)
                    set_repo_commit_to_db (repo->id, repo->name, repo->last_modify,
                                           repo->version, (repo->encrypted ? 1 : 0),
                                           repo->last_modifier);
            }
            if (repo->is_corrupted) {
                syncw_repo_unref (repo);
                continue;
            }
        }
        if (repo != NULL)
            ret = g_list_prepend (ret, repo);
    }
    g_list_free (repo_list);

    return ret;
}

GList *
syncw_repo_manager_get_repo_id_list (SyncwRepoManager *mgr)
{
    GList *ret = NULL;
    char sql[256];

    snprintf (sql, 256, "SELECT repo_id FROM Repo");

    if (syncwerk_server_db_foreach_selected_row (mgr->syncw->db, sql, 
                                      collect_repo_id, &ret) < 0)
        return NULL;

    return ret;
}

typedef struct FileCount {
    char *repo_id;
    gint64 file_count;
} FileCount;

static void
free_file_count (gpointer data)
{
    if (!data)
        return;

    FileCount *file_count = data;
    g_free (file_count->repo_id);
    g_free (file_count);
}

static gboolean
get_file_count_cb (SyncwDBRow *row, void *data)
{
    GList **file_counts = data;
    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    gint64 fcount = syncwerk_server_db_row_get_column_int64 (row, 1);

    FileCount *file_count = g_new0 (FileCount, 1);
    file_count->repo_id = g_strdup (repo_id);
    file_count->file_count = fcount;
    *file_counts = g_list_prepend (*file_counts, file_count);

    return TRUE;
}

GList *
syncw_repo_manager_get_repo_list (SyncwRepoManager *mgr, int start, int limit)
{
    GList *file_counts = NULL, *ptr;
    GList *ret = NULL;
    SyncwRepo *repo;
    FileCount *file_count;
    int rc;

    if (start == -1 && limit == -1)
        rc = syncwerk_server_db_statement_foreach_row (mgr->syncw->db,
                                            "SELECT r.repo_id, c.file_count FROM Repo r LEFT JOIN RepoFileCount c "
                                            "ON r.repo_id = c.repo_id",
                                            get_file_count_cb, &file_counts,
                                            0);
    else
        rc = syncwerk_server_db_statement_foreach_row (mgr->syncw->db,
                                            "SELECT r.repo_id, c.file_count FROM Repo r LEFT JOIN RepoFileCount c "
                                            "ON r.repo_id = c.repo_id ORDER BY r.repo_id LIMIT ? OFFSET ?",
                                            get_file_count_cb, &file_counts,
                                            2, "int", limit, "int", start);

    if (rc < 0)
        return NULL;

    for (ptr = file_counts; ptr; ptr = ptr->next) {
        file_count = ptr->data;
        repo = syncw_repo_manager_get_repo_ex (mgr, file_count->repo_id);
        if (repo != NULL) {
            repo->file_count = file_count->file_count;
            ret = g_list_prepend (ret, repo);
        }
    }

    g_list_free_full (file_counts, free_file_count);

    return ret;
}

gint64
syncw_repo_manager_count_repos (SyncwRepoManager *mgr, GError **error)
{
    gint64 num = syncwerk_server_db_get_int64 (mgr->syncw->db,
                                    "SELECT COUNT(repo_id) FROM Repo");
    if (num < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to count repos from db");
    }

    return num;
}

GList *
syncw_repo_manager_get_repo_ids_by_owner (SyncwRepoManager *mgr,
                                         const char *email)
{
    GList *ret = NULL;
    char *sql;

    sql = "SELECT repo_id FROM RepoOwner WHERE owner_id=?";

    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, 
                                       collect_repo_id, &ret,
                                       1, "string", email) < 0) {
        string_list_free (ret);
        return NULL;
    }

    return ret;
}

static gboolean
collect_trash_repo (SyncwDBRow *row, void *data)
{
    GList **trash_repos = data;
    const char *repo_id;
    const char *repo_name;
    const char *head_id;
    const char *owner_id;
    gint64 size;
    gint64 del_time;

    repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    repo_name = syncwerk_server_db_row_get_column_text (row, 1);
    head_id = syncwerk_server_db_row_get_column_text (row, 2);
    owner_id = syncwerk_server_db_row_get_column_text (row, 3);
    size = syncwerk_server_db_row_get_column_int64 (row, 4);
    del_time = syncwerk_server_db_row_get_column_int64 (row, 5);


    if (!repo_id || !repo_name || !head_id || !owner_id)
        return FALSE;

    SyncwerkTrashRepo *trash_repo = g_object_new (SYNCWERK_TYPE_TRASH_REPO,
                                                 "repo_id", repo_id,
                                                 "repo_name", repo_name,
                                                 "head_id", head_id,
                                                 "owner_id", owner_id,
                                                 "size", size,
                                                 "del_time", del_time,
                                                 NULL);
    if (!trash_repo)
        return FALSE;

    SyncwCommit *commit = syncw_commit_manager_get_commit_compatible (syncw->commit_mgr,
                                                                    repo_id, head_id);
    if (!commit) {
        syncw_warning ("Commit %s not found in repo %s\n", head_id, repo_id);
        g_object_unref (trash_repo);
        return FALSE;
    }
    g_object_set (trash_repo, "encrypted", commit->encrypted, NULL);
    syncw_commit_unref (commit);

    *trash_repos = g_list_prepend (*trash_repos, trash_repo);

    return TRUE;
}

GList *
syncw_repo_manager_get_trash_repo_list (SyncwRepoManager *mgr,
                                       int start,
                                       int limit,
                                       GError **error)
{
    GList *trash_repos = NULL;
    int rc;

    if (start == -1 && limit == -1)
        rc = syncwerk_server_db_statement_foreach_row (mgr->syncw->db,
                                            "SELECT repo_id, repo_name, head_id, owner_id, "
                                            "size, del_time FROM RepoTrash ORDER BY del_time DESC",
                                            collect_trash_repo, &trash_repos,
                                            0);
    else
        rc = syncwerk_server_db_statement_foreach_row (mgr->syncw->db,
                                            "SELECT repo_id, repo_name, head_id, owner_id, "
                                            "size, del_time FROM RepoTrash "
                                            "ORDER BY del_time DESC LIMIT ? OFFSET ?",
                                            collect_trash_repo, &trash_repos,
                                            2, "int", limit, "int", start);

    if (rc < 0) {
        while (trash_repos) {
            g_object_unref (trash_repos->data);
            trash_repos = g_list_delete_link (trash_repos, trash_repos);
        }
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get trashed repo from db.");
        return NULL;
    }

    return g_list_reverse (trash_repos);
}

GList *
syncw_repo_manager_get_trash_repos_by_owner (SyncwRepoManager *mgr,
                                            const char *owner,
                                            GError **error)
{
    GList *trash_repos = NULL;
    int rc;

    rc = syncwerk_server_db_statement_foreach_row (mgr->syncw->db,
                                        "SELECT repo_id, repo_name, head_id, owner_id, "
                                        "size, del_time FROM RepoTrash WHERE owner_id = ?",
                                        collect_trash_repo, &trash_repos,
                                        1, "string", owner);

    if (rc < 0) {
        while (trash_repos) {
            g_object_unref (trash_repos->data);
            trash_repos = g_list_delete_link (trash_repos, trash_repos);
        }
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get trashed repo from db.");
        return NULL;
    }

    return trash_repos;
}

SyncwerkTrashRepo *
syncw_repo_manager_get_repo_from_trash (SyncwRepoManager *mgr,
                                       const char *repo_id)
{
    SyncwerkTrashRepo *ret = NULL;
    GList *trash_repos = NULL;
    char *sql;
    int rc;

    sql = "SELECT repo_id, repo_name, head_id, owner_id, size, del_time FROM RepoTrash "
        "WHERE repo_id = ?";
    rc = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                        collect_trash_repo, &trash_repos,
                                        1, "string", repo_id);
    if (rc < 0)
        return NULL;

    /* There should be only one results, since repo_id is a PK. */
    if (trash_repos)
        ret = trash_repos->data;

    g_list_free (trash_repos);
    return ret;
}

int
syncw_repo_manager_del_repo_from_trash (SyncwRepoManager *mgr,
                                       const char *repo_id,
                                       GError **error)
{
    /* As long as the repo is successfully moved into GarbageRepo table,
     * we consider this operation successful.
     */
    if (add_deleted_repo_record (mgr, repo_id) < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "DB error: Add deleted record");
        return -1;
    }

    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoTrash WHERE repo_id = ?",
                             1, "string", repo_id);

    syncwerk_server_db_statement_query (mgr->syncw->db,
                             "DELETE FROM RepoInfo WHERE repo_id = ?",
                             1, "string", repo_id);

    return 0;
}

int
syncw_repo_manager_empty_repo_trash (SyncwRepoManager *mgr, GError **error)
{
    GList *trash_repos = NULL, *ptr;
    SyncwerkTrashRepo *repo;

    trash_repos = syncw_repo_manager_get_trash_repo_list (mgr, -1, -1, error);
    if (*error)
        return -1;

    for (ptr = trash_repos; ptr; ptr = ptr->next) {
        repo = ptr->data;
        syncw_repo_manager_del_repo_from_trash (mgr,
                                               syncwerk_trash_repo_get_repo_id(repo),
                                               NULL);
        g_object_unref (repo);
    }

    g_list_free (trash_repos);
    return 0;
}

int
syncw_repo_manager_empty_repo_trash_by_owner (SyncwRepoManager *mgr,
                                             const char *owner,
                                             GError **error)
{
    GList *trash_repos = NULL, *ptr;
    SyncwerkTrashRepo *repo;

    trash_repos = syncw_repo_manager_get_trash_repos_by_owner (mgr, owner, error);
    if (*error)
        return -1;

    for (ptr = trash_repos; ptr; ptr = ptr->next) {
        repo = ptr->data;
        syncw_repo_manager_del_repo_from_trash (mgr,
                                               syncwerk_trash_repo_get_repo_id(repo),
                                               NULL);
        g_object_unref (repo);
    }

    g_list_free (trash_repos);
    return 0;
}

int
syncw_repo_manager_restore_repo_from_trash (SyncwRepoManager *mgr,
                                           const char *repo_id,
                                           GError **error)
{
    SyncwerkTrashRepo *repo = NULL;
    int ret = 0;
    gboolean exists = FALSE;
    gboolean db_err;

    repo = syncw_repo_manager_get_repo_from_trash (mgr, repo_id);
    if (!repo) {
        syncw_warning ("Repo %.8s not found in trash.\n", repo_id);
        return -1;
    }

    SyncwDBTrans *trans = syncwerk_server_db_begin_transaction (mgr->syncw->db);

    exists = syncwerk_server_db_trans_check_for_existence (trans,
                                                "SELECT 1 FROM Repo WHERE repo_id=?",
                                                &db_err, 1, "string", repo_id);

    if (!exists) {
        ret = syncwerk_server_db_trans_query (trans,
                                   "INSERT INTO Repo(repo_id) VALUES (?)",
                                   1, "string", repo_id) < 0;
        if (ret < 0) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "DB error: Insert Repo.");
            syncwerk_server_db_rollback (trans);
            syncwerk_server_db_trans_close (trans);
            goto out;
        }
    }

    exists = syncwerk_server_db_trans_check_for_existence (trans,
                                                "SELECT 1 FROM RepoOwner WHERE repo_id=?",
                                                &db_err, 1, "string", repo_id);

    if (!exists) {
        ret = syncwerk_server_db_trans_query (trans,
                                   "INSERT INTO RepoOwner (repo_id, owner_id) VALUES (?, ?)",
                                   2, "string", repo_id,
                                   "string", syncwerk_trash_repo_get_owner_id(repo));
        if (ret < 0) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "DB error: Insert Repo Owner.");
            syncwerk_server_db_rollback (trans);
            syncwerk_server_db_trans_close (trans);
            goto out;
        }
    }

    exists = syncwerk_server_db_trans_check_for_existence (trans,
                                                "SELECT 1 FROM Branch WHERE repo_id=?",
                                                &db_err, 1, "string", repo_id);
    if (!exists) {
        ret = syncwerk_server_db_trans_query (trans,
                                   "INSERT INTO Branch (name, repo_id, commit_id) VALUES ('master', ?, ?)",
                                   2, "string", repo_id,
                                   "string", syncwerk_trash_repo_get_head_id(repo));
        if (ret < 0) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "DB error: Insert Branch.");
            syncwerk_server_db_rollback (trans);
            syncwerk_server_db_trans_close (trans);
            goto out;
        }
    }

    exists = syncwerk_server_db_trans_check_for_existence (trans,
                                                "SELECT 1 FROM RepoHead WHERE repo_id=?",
                                                &db_err, 1, "string", repo_id);
    if (!exists) {
        ret = syncwerk_server_db_trans_query (trans,
                                   "INSERT INTO RepoHead (repo_id, branch_name) VALUES (?, 'master')",
                                   1, "string", repo_id);
        if (ret < 0) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "DB error: Set RepoHead.");
            syncwerk_server_db_rollback (trans);
            syncwerk_server_db_trans_close (trans);
            goto out;
        }
    }

    // Restore repo size
    exists = syncwerk_server_db_trans_check_for_existence (trans,
                                                "SELECT 1 FROM RepoSize WHERE repo_id=?",
                                                &db_err, 1, "string", repo_id);

    if (!exists) {
        ret = syncwerk_server_db_trans_query (trans,
                                   "INSERT INTO RepoSize (repo_id, size, head_id) VALUES (?, ?, ?)",
                                   3, "string", repo_id,
                                   "int64", syncwerk_trash_repo_get_size (repo),
                                   "string", syncwerk_trash_repo_get_head_id (repo));
        if (ret < 0) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "DB error: Insert Repo Size.");
            syncwerk_server_db_rollback (trans);
            syncwerk_server_db_trans_close (trans);
            goto out;
        }
    }

    ret = syncwerk_server_db_trans_query (trans,
                               "DELETE FROM RepoTrash WHERE repo_id = ?",
                               1, "string", repo_id);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "DB error: delete from RepoTrash.");
        syncwerk_server_db_rollback (trans);
        syncwerk_server_db_trans_close (trans);
        goto out;
    }

    if (syncwerk_server_db_commit (trans) < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "DB error: Failed to commit.");
        syncwerk_server_db_rollback (trans);
        ret = -1;
    }

    syncwerk_server_db_trans_close (trans);

out:
    g_object_unref (repo);
    return ret;
}

/* Web access permission. */

int
syncw_repo_manager_set_access_property (SyncwRepoManager *mgr, const char *repo_id,
                                       const char *ap)
{
    int rc;

    if (syncw_repo_manager_query_access_property (mgr, repo_id) == NULL) {
        rc = syncwerk_server_db_statement_query (mgr->syncw->db,
                                      "INSERT INTO WebAP (repo_id, access_property) VALUES (?, ?)",
                                      2, "string", repo_id, "string", ap);
    } else {
        rc = syncwerk_server_db_statement_query (mgr->syncw->db,
                                      "UPDATE WebAP SET access_property=? "
                                      "WHERE repo_id=?",
                                      2, "string", ap, "string", repo_id);
    }

    if (rc < 0) {
        syncw_warning ("DB error when set access property for repo %s, %s.\n", repo_id, ap);
        return -1;
    }
    
    return 0;
}

static gboolean
get_ap (SyncwDBRow *row, void *data)
{
    char **ap = data;

    *ap = g_strdup (syncwerk_server_db_row_get_column_text (row, 0));

    return FALSE;
}

char *
syncw_repo_manager_query_access_property (SyncwRepoManager *mgr, const char *repo_id)
{
    char *sql;
    char *ret = NULL;

    sql =  "SELECT access_property FROM WebAP WHERE repo_id=?";
 
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, get_ap, &ret,
                                       1, "string", repo_id) < 0) {
        syncw_warning ("DB error when get access property for repo %s.\n", repo_id);
        return NULL;
    }

    return ret;
}

/* Group repos. */

int
syncw_repo_manager_add_group_repo (SyncwRepoManager *mgr,
                                  const char *repo_id,
                                  int group_id,
                                  const char *owner,
                                  const char *permission,
                                  GError **error)
{
    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                                 "INSERT INTO RepoGroup (repo_id, group_id, user_name, permission) VALUES (?, ?, ?, ?)",
                                 4, "string", repo_id, "int", group_id,
                                 "string", owner, "string", permission) < 0)
        return -1;

    return 0;
}

int
syncw_repo_manager_del_group_repo (SyncwRepoManager *mgr,
                                  const char *repo_id,
                                  int group_id,
                                  GError **error)
{
    return syncwerk_server_db_statement_query (mgr->syncw->db,
                                    "DELETE FROM RepoGroup WHERE group_id=? "
                                    "AND repo_id=?",
                                    2, "int", group_id, "string", repo_id);
}

static gboolean
get_group_ids_cb (SyncwDBRow *row, void *data)
{
    GList **plist = data;

    int group_id = syncwerk_server_db_row_get_column_int (row, 0);

    *plist = g_list_prepend (*plist, (gpointer)(long)group_id);

    return TRUE;
}

GList *
syncw_repo_manager_get_groups_by_repo (SyncwRepoManager *mgr,
                                      const char *repo_id,
                                      GError **error)
{
    char *sql;
    GList *group_ids = NULL;
    
    sql =  "SELECT group_id FROM RepoGroup WHERE repo_id = ?";
    
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, get_group_ids_cb,
                                       &group_ids, 1, "string", repo_id) < 0) {
        g_list_free (group_ids);
        return NULL;
    }

    return g_list_reverse (group_ids);
}

static gboolean
get_group_perms_cb (SyncwDBRow *row, void *data)
{
    GList **plist = data;
    GroupPerm *perm = g_new0 (GroupPerm, 1);

    perm->group_id = syncwerk_server_db_row_get_column_int (row, 0);
    const char *permission = syncwerk_server_db_row_get_column_text(row, 1);
    g_strlcpy (perm->permission, permission, sizeof(perm->permission));

    *plist = g_list_prepend (*plist, perm);

    return TRUE;
}

GList *
syncw_repo_manager_get_group_perm_by_repo (SyncwRepoManager *mgr,
                                          const char *repo_id,
                                          GError **error)
{
    char *sql;
    GList *group_perms = NULL, *p;
    
    sql = "SELECT group_id, permission FROM RepoGroup WHERE repo_id = ?";
    
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, get_group_perms_cb,
                                       &group_perms, 1, "string", repo_id) < 0) {
        for (p = group_perms; p != NULL; p = p->next)
            g_free (p->data);
        g_list_free (group_perms);
        return NULL;
    }

    return g_list_reverse (group_perms);
}

int
syncw_repo_manager_set_group_repo_perm (SyncwRepoManager *mgr,
                                       const char *repo_id,
                                       int group_id,
                                       const char *permission,
                                       GError **error)
{
    return syncwerk_server_db_statement_query (mgr->syncw->db,
                                    "UPDATE RepoGroup SET permission=? WHERE "
                                    "repo_id=? AND group_id=?",
                                    3, "string", permission, "string", repo_id,
                                    "int", group_id);
}

int
syncw_repo_manager_set_subdir_group_perm_by_path (SyncwRepoManager *mgr,
                                                 const char *repo_id,
                                                 const char *username,
                                                 int group_id,
                                                 const char *permission,
                                                 const char *path)
{
    return syncwerk_server_db_statement_query (mgr->syncw->db,
                                    "UPDATE RepoGroup SET permission=? WHERE repo_id IN "
                                    "(SELECT repo_id FROM VirtualRepo WHERE origin_repo=? AND path=?) "
                                    "AND group_id=? AND user_name=?",
                                    5, "string", permission,
                                    "string", repo_id,
                                    "string", path,
                                    "int", group_id,
                                    "string", username);
}
static gboolean
get_group_repoids_cb (SyncwDBRow *row, void *data)
{
    GList **p_list = data;

    char *repo_id = g_strdup ((const char *)syncwerk_server_db_row_get_column_text (row, 0));

    *p_list = g_list_prepend (*p_list, repo_id);

    return TRUE;
}

GList *
syncw_repo_manager_get_group_repoids (SyncwRepoManager *mgr,
                                     int group_id,
                                     GError **error)
{
    char *sql;
    GList *repo_ids = NULL;

    sql =  "SELECT repo_id FROM RepoGroup WHERE group_id = ?";
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, get_group_repoids_cb,
                                       &repo_ids, 1, "int", group_id) < 0)
        return NULL;

    return g_list_reverse (repo_ids);
}

static gboolean
get_group_repos_cb (SyncwDBRow *row, void *data)
{
    GList **p_list = data;
    SyncwerkRepo *srepo = NULL;

    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    const char *vrepo_id = syncwerk_server_db_row_get_column_text (row, 1);
    int group_id = syncwerk_server_db_row_get_column_int (row, 2);
    const char *user_name = syncwerk_server_db_row_get_column_text (row, 3);
    const char *permission = syncwerk_server_db_row_get_column_text (row, 4);
    const char *commit_id = syncwerk_server_db_row_get_column_text (row, 5);
    gint64 size = syncwerk_server_db_row_get_column_int64 (row, 6);

    char *user_name_l = g_ascii_strdown (user_name, -1);

    srepo = g_object_new (SYNCWERK_TYPE_REPO,
                          "share_type", "group",
                          "repo_id", repo_id,
                          "id", repo_id,
                          "head_cmmt_id", commit_id,
                          "group_id", group_id,
                          "user", user_name_l,
                          "permission", permission,
                          "is_virtual", (vrepo_id != NULL),
                          "size", size,
                          NULL);
    g_free (user_name_l);

    if (srepo != NULL) {
        if (vrepo_id) {
            const char *origin_repo_id = syncwerk_server_db_row_get_column_text (row, 7);
            const char *origin_path = syncwerk_server_db_row_get_column_text (row, 8);
            const char *origin_repo_name = syncwerk_server_db_row_get_column_text (row, 9);
            g_object_set (srepo, "store_id", origin_repo_id,
                          "origin_repo_id", origin_repo_id,
                          "origin_repo_name", origin_repo_name,
                          "origin_path", origin_path, NULL);
        } else {
            g_object_set (srepo, "store_id", repo_id, NULL);
        }

        *p_list = g_list_prepend (*p_list, srepo);
    }

    return TRUE;
}

void
syncw_fill_repo_obj_from_commit (GList **repos)
{
    SyncwerkRepo *repo;
    SyncwCommit *commit;
    char *repo_id;
    char *commit_id;
    GList *p = *repos;
    GList *next;

    while (p) {
        repo = p->data;
        g_object_get (repo, "repo_id", &repo_id, "head_cmmt_id", &commit_id, NULL);
        commit = syncw_commit_manager_get_commit_compatible (syncw->commit_mgr,
                                                            repo_id, commit_id);
        if (!commit) {
            syncw_warning ("Commit %s not found in repo %s\n", commit_id, repo_id);
            g_object_unref (repo);
            next = p->next;
            *repos = g_list_delete_link (*repos, p);
            p = next;
        } else {
            g_object_set (repo, "name", commit->repo_name, "desc", commit->repo_desc,
                          "encrypted", commit->encrypted, "magic", commit->magic,
                          "enc_version", commit->enc_version, "root", commit->root_id,
                          "version", commit->version, "last_modify", commit->ctime,
                          NULL);
            g_object_set (repo,
                          "repo_name", commit->repo_name, "repo_desc", commit->repo_desc,
                          "last_modified", commit->ctime, "last_modify", commit->ctime,
                          "repaired", commit->repaired, "last_modifier", commit->creator_name, NULL);
            if (commit->encrypted && commit->enc_version == 2)
                g_object_set (repo, "random_key", commit->random_key, NULL);

            p = p->next;
        }
        g_free (repo_id);
        g_free (commit_id);
        syncw_commit_unref (commit);
    }
}

GList *
syncw_repo_manager_get_repos_by_group (SyncwRepoManager *mgr,
                                      int group_id,
                                      GError **error)
{
    char *sql;
    GList *repos = NULL;
    GList *p;

    sql = "SELECT RepoGroup.repo_id, v.repo_id, "
        "group_id, user_name, permission, commit_id, s.size, "
        "v.origin_repo, v.path ,"
        "(SELECT name FROM RepoInfo WHERE repo_id=v.origin_repo) "
        "FROM RepoGroup LEFT JOIN VirtualRepo v ON "
        "RepoGroup.repo_id = v.repo_id "
        "LEFT JOIN RepoSize s ON RepoGroup.repo_id = s.repo_id, "
        "Branch WHERE group_id = ? AND "
        "RepoGroup.repo_id = Branch.repo_id AND "
        "Branch.name = 'master'";

    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, get_group_repos_cb,
                                       &repos, 1, "int", group_id) < 0) {
        for (p = repos; p; p = p->next) {
            g_object_unref (p->data);
        }
        g_list_free (repos);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get repos by group from db.");
        return NULL;
    }

    syncw_fill_repo_obj_from_commit (&repos);

    return g_list_reverse (repos);
}

GList *
syncw_repo_manager_get_group_repos_by_owner (SyncwRepoManager *mgr,
                                            const char *owner,
                                            GError **error)
{
    char *sql;
    GList *repos = NULL;
    GList *p;

    sql = "SELECT RepoGroup.repo_id, v.repo_id, "
        "group_id, user_name, permission, commit_id, s.size, "
        "v.origin_repo, v.path, "
        "(SELECT name FROM RepoInfo WHERE repo_id=v.origin_repo) "
        "FROM RepoGroup LEFT JOIN VirtualRepo v ON "
        "RepoGroup.repo_id = v.repo_id "
        "LEFT JOIN RepoSize s ON RepoGroup.repo_id = s.repo_id, "
        "Branch WHERE user_name = ? AND "
        "RepoGroup.repo_id = Branch.repo_id AND "
        "Branch.name = 'master'";
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, get_group_repos_cb,
                                       &repos, 1, "string", owner) < 0) {
        for (p = repos; p; p = p->next) {
            g_object_unref (p->data);
        }
        g_list_free (repos);
        return NULL;
    }

    syncw_fill_repo_obj_from_commit (&repos);

    return g_list_reverse (repos);
}

static gboolean
get_group_repo_owner (SyncwDBRow *row, void *data)
{
    char **share_from = data;

    const char *owner = (const char *) syncwerk_server_db_row_get_column_text (row, 0);
    *share_from = g_ascii_strdown (owner, -1);
    /* There should be only one result. */
    return FALSE;
}

char *
syncw_repo_manager_get_group_repo_owner (SyncwRepoManager *mgr,
                                        const char *repo_id,
                                        GError **error)
{
    char *sql;
    char *ret = NULL;

    sql = "SELECT user_name FROM RepoGroup WHERE repo_id = ?";
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                       get_group_repo_owner, &ret,
                                       1, "string", repo_id) < 0) {
        syncw_warning ("DB error when get repo share from for repo %s.\n",
                   repo_id);
        return NULL;
    }

    return ret;
}

int
syncw_repo_manager_remove_group_repos (SyncwRepoManager *mgr,
                                      int group_id,
                                      const char *owner,
                                      GError **error)
{
    SyncwDB *db = mgr->syncw->db;
    int rc;

    if (!owner) {
        rc = syncwerk_server_db_statement_query (db, "DELETE FROM RepoGroup WHERE group_id=?",
                                      1, "int", group_id);
    } else {
        rc = syncwerk_server_db_statement_query (db,
                                      "DELETE FROM RepoGroup WHERE group_id=? AND "
                                      "user_name = ?",
                                      2, "int", group_id, "string", owner);
    }

    return rc;
}

/* Inner public repos */

int
syncw_repo_manager_set_inner_pub_repo (SyncwRepoManager *mgr,
                                      const char *repo_id,
                                      const char *permission)
{
    SyncwDB *db = mgr->syncw->db;
    char sql[256];

    if (syncwerk_server_db_type(db) == SYNCW_DB_TYPE_PGSQL) {
        gboolean err;
        snprintf(sql, sizeof(sql),
                 "SELECT repo_id FROM InnerPubRepo WHERE repo_id=?");
        if (syncwerk_server_db_statement_exists (db, sql, &err,
                                      1, "string", repo_id))
            snprintf(sql, sizeof(sql),
                     "UPDATE InnerPubRepo SET permission='%s' "
                     "WHERE repo_id='%s'", permission, repo_id);
        else
            snprintf(sql, sizeof(sql),
                     "INSERT INTO InnerPubRepo (repo_id, permission) VALUES "
                     "('%s', '%s')", repo_id, permission);
        if (err)
            return -1;
        return syncwerk_server_db_query (db, sql);
    } else {
        return syncwerk_server_db_statement_query (db,
                                        "REPLACE INTO InnerPubRepo (repo_id, permission) VALUES (?, ?)",
                                        2, "string", repo_id, "string", permission);
    }

    return -1;
}

int
syncw_repo_manager_unset_inner_pub_repo (SyncwRepoManager *mgr,
                                        const char *repo_id)
{
    return syncwerk_server_db_statement_query (mgr->syncw->db,
                                    "DELETE FROM InnerPubRepo WHERE repo_id = ?",
                                    1, "string", repo_id);
}

gboolean
syncw_repo_manager_is_inner_pub_repo (SyncwRepoManager *mgr,
                                     const char *repo_id)
{
    gboolean db_err = FALSE;

    return syncwerk_server_db_statement_exists (mgr->syncw->db,
                                     "SELECT repo_id FROM InnerPubRepo WHERE repo_id=?",
                                     &db_err, 1, "string", repo_id);
}

static gboolean
collect_public_repos (SyncwDBRow *row, void *data)
{
    GList **ret = (GList **)data;
    SyncwerkRepo *srepo;
    const char *repo_id, *vrepo_id, *owner, *permission, *commit_id;
    gint64 size;

    repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    vrepo_id = syncwerk_server_db_row_get_column_text (row, 1);
    owner = syncwerk_server_db_row_get_column_text (row, 2);
    permission = syncwerk_server_db_row_get_column_text (row, 3);
    commit_id = syncwerk_server_db_row_get_column_text (row, 4);
    size = syncwerk_server_db_row_get_column_int64 (row, 5);

    char *owner_l = g_ascii_strdown (owner, -1);

    srepo = g_object_new (SYNCWERK_TYPE_REPO,
                          "share_type", "public",
                          "repo_id", repo_id,
                          "id", repo_id,
                          "head_cmmt_id", commit_id,
                          "permission", permission,
                          "user", owner_l,
                          "is_virtual", (vrepo_id != NULL),
                          "size", size,
                          NULL);
    g_free (owner_l);

    if (srepo) {
        if (vrepo_id) {
            const char *origin_repo_id = syncwerk_server_db_row_get_column_text (row, 6);
            const char *origin_path = syncwerk_server_db_row_get_column_text (row, 7);
            g_object_set (srepo, "store_id", origin_repo_id,
                          "origin_repo_id", origin_repo_id,
                          "origin_path", origin_path, NULL);
        } else {
            g_object_set (srepo, "store_id", repo_id, NULL);
        }

        *ret = g_list_prepend (*ret, srepo);
    }

    return TRUE;
}

GList *
syncw_repo_manager_list_inner_pub_repos (SyncwRepoManager *mgr)
{
    GList *ret = NULL, *p;
    char *sql;

    sql = "SELECT InnerPubRepo.repo_id, VirtualRepo.repo_id, "
        "owner_id, permission, commit_id, s.size, "
        "VirtualRepo.origin_repo, VirtualRepo.path "
        "FROM InnerPubRepo LEFT JOIN VirtualRepo ON "
        "InnerPubRepo.repo_id=VirtualRepo.repo_id "
        "LEFT JOIN RepoSize s ON InnerPubRepo.repo_id = s.repo_id, RepoOwner, Branch "
        "WHERE InnerPubRepo.repo_id=RepoOwner.repo_id AND "
        "InnerPubRepo.repo_id = Branch.repo_id AND Branch.name = 'master'";

    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                       collect_public_repos, &ret,
                                       0) < 0) {
        for (p = ret; p != NULL; p = p->next)
            g_object_unref (p->data);
        g_list_free (ret);
        return NULL;
    }

    syncw_fill_repo_obj_from_commit (&ret);

    return g_list_reverse (ret);
}

gint64
syncw_repo_manager_count_inner_pub_repos (SyncwRepoManager *mgr)
{
    char sql[256];

    snprintf (sql, 256, "SELECT COUNT(*) FROM InnerPubRepo");

    return syncwerk_server_db_get_int64(mgr->syncw->db, sql);
}

GList *
syncw_repo_manager_list_inner_pub_repos_by_owner (SyncwRepoManager *mgr,
                                                 const char *user)
{
    GList *ret = NULL, *p;
    char *sql;

    sql = "SELECT InnerPubRepo.repo_id, VirtualRepo.repo_id, "
        "owner_id, permission, commit_id, s.size, "
        "VirtualRepo.origin_repo, VirtualRepo.path "
        "FROM InnerPubRepo LEFT JOIN VirtualRepo ON "
        "InnerPubRepo.repo_id=VirtualRepo.repo_id "
        "LEFT JOIN RepoSize s ON InnerPubRepo.repo_id = s.repo_id, RepoOwner, Branch "
        "WHERE InnerPubRepo.repo_id=RepoOwner.repo_id AND owner_id=? "
        "AND InnerPubRepo.repo_id = Branch.repo_id AND Branch.name = 'master'";

    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                       collect_public_repos, &ret,
                                       1, "string", user) < 0) {
        for (p = ret; p != NULL; p = p->next)
            g_object_unref (p->data);
        g_list_free (ret);
        return NULL;
    }

    syncw_fill_repo_obj_from_commit (&ret);

    return g_list_reverse (ret);
}

char *
syncw_repo_manager_get_inner_pub_repo_perm (SyncwRepoManager *mgr,
                                           const char *repo_id)
{
    char *sql;

    sql = "SELECT permission FROM InnerPubRepo WHERE repo_id=?";
    return syncwerk_server_db_statement_get_string(mgr->syncw->db, sql, 1, "string", repo_id);
}


int
syncw_repo_manager_is_valid_filename (SyncwRepoManager *mgr,
                                     const char *repo_id,
                                     const char *filename,
                                     GError **error)
{
    if (should_ignore_file(filename, NULL))
        return 0;
    else
        return 1;
}

static int
create_repo_common (SyncwRepoManager *mgr,
                    const char *repo_id,
                    const char *repo_name,
                    const char *repo_desc,
                    const char *user,
                    const char *magic,
                    const char *random_key,
                    int enc_version,
                    GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *commit = NULL;
    SyncwBranch *master = NULL;
    int ret = -1;

    if (enc_version != 2 && enc_version != -1) {
        syncw_warning ("Unsupported enc version %d.\n", enc_version);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Unsupported encryption version");
        return -1;
    }

    if (enc_version == 2) {
        if (!magic || strlen(magic) != 64) {
            syncw_warning ("Bad magic.\n");
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                         "Bad magic");
            return -1;
        }
        if (!random_key || strlen(random_key) != 96) {
            syncw_warning ("Bad random key.\n");
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                         "Bad random key");
            return -1;
        }
    }

    repo = syncw_repo_new (repo_id, repo_name, repo_desc);

    repo->no_local_history = TRUE;
    if (enc_version == 2) {
        repo->encrypted = TRUE;
        repo->enc_version = enc_version;
        memcpy (repo->magic, magic, 64);
        memcpy (repo->random_key, random_key, 96);
    }

    repo->version = CURRENT_REPO_VERSION;
    memcpy (repo->store_id, repo_id, 36);

    commit = syncw_commit_new (NULL, repo->id,
                              EMPTY_SHA1, /* root id */
                              user, /* creator */
                              EMPTY_SHA1, /* creator id */
                              "Created library",  /* description */
                              0);         /* ctime */

    syncw_repo_to_commit (repo, commit);
    if (syncw_commit_manager_add_commit (syncw->commit_mgr, commit) < 0) {
        syncw_warning ("Failed to add commit.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to add commit");
        goto out;
    }

    master = syncw_branch_new ("master", repo->id, commit->commit_id);
    if (syncw_branch_manager_add_branch (syncw->branch_mgr, master) < 0) {
        syncw_warning ("Failed to add branch.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to add branch");
        goto out;
    }

    if (syncw_repo_set_head (repo, master) < 0) {
        syncw_warning ("Failed to set repo head.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to set repo head.");
        goto out;
    }

    if (syncw_repo_manager_add_repo (mgr, repo) < 0) {
        syncw_warning ("Failed to add repo.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to add repo.");
        goto out;
    }

    syncw_repo_manager_update_repo_info (mgr, repo->id, repo->head->commit_id);

    ret = 0;
out:
    if (repo)
        syncw_repo_unref (repo);
    if (commit)
        syncw_commit_unref (commit);
    if (master)
        syncw_branch_unref (master);
    
    return ret;    
}

char *
syncw_repo_manager_create_new_repo (SyncwRepoManager *mgr,
                                   const char *repo_name,
                                   const char *repo_desc,
                                   const char *owner_email,
                                   const char *passwd,
                                   GError **error)
{
    char *repo_id = NULL;
    char magic[65], random_key[97];

    repo_id = gen_uuid ();

    if (passwd && passwd[0] != 0) {
        syncwerk_generate_magic (2, repo_id, passwd, magic);
        syncwerk_generate_random_key (passwd, random_key);
    }

    int rc;
    if (passwd)
        rc = create_repo_common (mgr, repo_id, repo_name, repo_desc, owner_email,
                                 magic, random_key, CURRENT_ENC_VERSION, error);
    else
        rc = create_repo_common (mgr, repo_id, repo_name, repo_desc, owner_email,
                                 NULL, NULL, -1, error);
    if (rc < 0)
        goto bad;

    if (syncw_repo_manager_set_repo_owner (mgr, repo_id, owner_email) < 0) {
        syncw_warning ("Failed to set repo owner.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to set repo owner.");
        goto bad;
    }

    return repo_id;
    
bad:
    if (repo_id)
        g_free (repo_id);
    return NULL;
}

char *
syncw_repo_manager_create_enc_repo (SyncwRepoManager *mgr,
                                   const char *repo_id,
                                   const char *repo_name,
                                   const char *repo_desc,
                                   const char *owner_email,
                                   const char *magic,
                                   const char *random_key,
                                   int enc_version,
                                   GError **error)
{
    if (!repo_id || !is_uuid_valid (repo_id)) {
        syncw_warning ("Invalid repo_id.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo id");
        return NULL;
    }

    if (syncw_repo_manager_repo_exists (mgr, repo_id)) {
        syncw_warning ("Repo %s exists, refuse to create.\n", repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Repo already exists");
        return NULL;
    }

    if (create_repo_common (mgr, repo_id, repo_name, repo_desc, owner_email,
                            magic, random_key, enc_version, error) < 0)
        return NULL;

    if (syncw_repo_manager_set_repo_owner (mgr, repo_id, owner_email) < 0) {
        syncw_warning ("Failed to set repo owner.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to set repo owner.");
        return NULL;
    }

    return g_strdup (repo_id);
}

static int reap_token (void *data)
{
    SyncwRepoManager *mgr = data;
    GHashTableIter iter;
    gpointer key, value;
    DecryptedToken *t;

    pthread_rwlock_wrlock (&mgr->priv->lock);

    gint64 now = (gint64)time(NULL);

    g_hash_table_iter_init (&iter, mgr->priv->decrypted_tokens);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        t = value;
        if (now >= t->reap_time)
            g_hash_table_iter_remove (&iter);
    }

    pthread_rwlock_unlock (&mgr->priv->lock);

    return TRUE;
}

static void decrypted_token_free (DecryptedToken *token)
{
    if (!token)
        return;
    g_free (token->token);
    g_free (token);
}

void
syncw_repo_manager_add_decrypted_token (SyncwRepoManager *mgr,
                                       const char *encrypted_token,
                                       const char *session_key,
                                       const char *decrypted_token)
{
    char key[256];
    DecryptedToken *token;

    snprintf (key, sizeof(key), "%s%s", encrypted_token, session_key);
    key[255] = 0;

    pthread_rwlock_wrlock (&mgr->priv->lock);

    token = g_new0 (DecryptedToken, 1);
    token->token = g_strdup(decrypted_token);
    token->reap_time = (gint64)time(NULL) + DECRYPTED_TOKEN_TTL;

    g_hash_table_insert (mgr->priv->decrypted_tokens,
                         g_strdup(key),
                         token);

    pthread_rwlock_unlock (&mgr->priv->lock);
}

char *
syncw_repo_manager_get_decrypted_token (SyncwRepoManager *mgr,
                                       const char *encrypted_token,
                                       const char *session_key)
{
    char key[256];
    DecryptedToken *token;

    snprintf (key, sizeof(key), "%s%s", encrypted_token, session_key);
    key[255] = 0;

    pthread_rwlock_rdlock (&mgr->priv->lock);
    token = g_hash_table_lookup (mgr->priv->decrypted_tokens, key);
    pthread_rwlock_unlock (&mgr->priv->lock);

    if (token)
        return g_strdup(token->token);
    return NULL;
}

static gboolean
get_shared_users (SyncwDBRow *row, void *data)
{
    GList **shared_users = data;
    const char *user = syncwerk_server_db_row_get_column_text (row, 0);
    const char *perm = syncwerk_server_db_row_get_column_text (row, 1);
    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 2);

    SyncwerkSharedUser *uobj = g_object_new (SYNCWERK_TYPE_SHARED_USER,
                                            "repo_id", repo_id,
                                            "user", user,
                                            "perm", perm,
                                            NULL);
    *shared_users = g_list_prepend (*shared_users, uobj);

    return TRUE;
}

GList *
syncw_repo_manager_get_shared_users_for_subdir (SyncwRepoManager *mgr,
                                               const char *repo_id,
                                               const char *path,
                                               const char *from_user,
                                               GError **error)
{
    GList *shared_users = NULL;
    int ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db,
                                             "SELECT to_email, permission, v.repo_id "
                                             "FROM SharedRepo s, VirtualRepo v "
                                             "WHERE s.repo_id = v.repo_id AND v.origin_repo = ? "
                                             "AND v.path = ? AND s.from_email = ?",
                                             get_shared_users, &shared_users, 3, "string", repo_id,
                                             "string", path, "string", from_user);
    if (ret < 0) {
        syncw_warning ("Failed to get shared users for %.8s(%s).\n", repo_id, path);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get shared users for subdir from db");
        while (shared_users) {
            g_object_unref (shared_users->data);
            shared_users = g_list_delete_link (shared_users, shared_users);
        }
        return NULL;
    }

    return shared_users;
}

static gboolean
get_shared_groups (SyncwDBRow *row, void *data)
{
    GList **shared_groups = data;
    int group = syncwerk_server_db_row_get_column_int (row, 0);
    const char *perm = syncwerk_server_db_row_get_column_text (row, 1);
    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 2);

    SyncwerkSharedGroup *gobj = g_object_new (SYNCWERK_TYPE_SHARED_GROUP,
                                             "repo_id", repo_id,
                                             "group_id", group,
                                             "perm", perm,
                                             NULL);

    *shared_groups = g_list_prepend (*shared_groups, gobj);

    return TRUE;
}

GList *
syncw_repo_manager_get_shared_groups_for_subdir (SyncwRepoManager *mgr,
                                                const char *repo_id,
                                                const char *path,
                                                const char *from_user,
                                                GError **error)
{
    GList *shared_groups = NULL;
    int ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db,
                                             "SELECT group_id, permission, v.repo_id "
                                             "FROM RepoGroup r, VirtualRepo v "
                                             "WHERE r.repo_id = v.repo_id AND v.origin_repo = ? "
                                             "AND v.path = ? AND r.user_name = ?",
                                             get_shared_groups, &shared_groups, 3, "string", repo_id,
                                             "string", path, "string", from_user);
    if (ret < 0) {
        syncw_warning ("Failed to get shared groups for %.8s(%s).\n", repo_id, path);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get shared groups fro subdir from db");
        while (shared_groups) {
            g_object_unref (shared_groups->data);
            shared_groups = g_list_delete_link (shared_groups, shared_groups);
        }
        return NULL;
    }

    return shared_groups;
}
int
syncw_repo_manager_edit_repo (const char *repo_id,
                             const char *name,
                             const char *description,
                             const char *user,
                             GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *commit = NULL, *parent = NULL;
    int ret = 0;

    if (!name && !description) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "At least one argument should be non-null");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

retry:
    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "No such library");
        return -1;
    }
    if (!name)
        name = repo->name;
    if (!description)
        description = repo->desc;

    /*
     * We only change repo_name or repo_desc, so just copy the head commit
     * and change these two fields.
     */
    parent = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                             repo->id, repo->version,
                                             repo->head->commit_id);
    if (!parent) {
        syncw_warning ("Failed to get commit %s:%s.\n",
                      repo->id, repo->head->commit_id);
        ret = -1;
        goto out;
    }
    if (!user) {
        user = parent->creator_name;
    }

    commit = syncw_commit_new (NULL,
                              repo->id,
                              parent->root_id,
                              user,
                              EMPTY_SHA1,
                              "Changed library name or description",
                              0);
    commit->parent_id = g_strdup(parent->commit_id);
    syncw_repo_to_commit (repo, commit);

    g_free (commit->repo_name);
    commit->repo_name = g_strdup(name);
    g_free (commit->repo_desc);
    commit->repo_desc = g_strdup(description);

    if (syncw_commit_manager_add_commit (syncw->commit_mgr, commit) < 0) {
        ret = -1;
        goto out;
    }

    syncw_branch_set_commit (repo->head, commit->commit_id);
    if (syncw_branch_manager_test_and_update_branch (syncw->branch_mgr,
                                                    repo->head,
                                                    parent->commit_id) < 0) {
        syncw_repo_unref (repo);
        syncw_commit_unref (commit);
        syncw_commit_unref (parent);
        repo = NULL;
        commit = NULL;
        parent = NULL;
        goto retry;
    }

out:
    syncw_commit_unref (commit);
    syncw_commit_unref (parent);
    syncw_repo_unref (repo);

    return ret;
}

gboolean
get_total_file_number_cb (SyncwDBRow *row, void *vdata)
{
    gint64 *data = (gint64 *)vdata;
    gint64 count = syncwerk_server_db_row_get_column_int64 (row, 0);
    *data = count;

    return FALSE;
}

gint64
syncw_get_total_file_number (GError **error)
{
    gint64 count = 0;
    int ret = syncwerk_server_db_statement_foreach_row (syncw->db,
                                             "SELECT SUM(file_count) FROM RepoFileCount f "
                                             "LEFT JOIN VirtualRepo v "
                                             "ON f.repo_id=v.repo_id "
                                             "WHERE v.repo_id IS NULL",
                                             get_total_file_number_cb,
                                             &count, 0);
    if (ret < 0) { 
        syncw_warning ("Failed to get total file number.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get total file number from db.");
        return -1;
    }

    return count;
}

gboolean
get_total_storage_cb(SyncwDBRow *row, void *vdata)
{
    gint64 *data = (gint64 *)vdata;
    gint64 size = syncwerk_server_db_row_get_column_int64 (row, 0);
    *data = size;

    return FALSE;
}

gint64
syncw_get_total_storage (GError **error)
{
    gint64 size = 0;
    int ret;
    if (syncwerk_server_db_type(syncw->db) == SYNCW_DB_TYPE_PGSQL) {
        ret = syncwerk_server_db_statement_foreach_row (syncw->db,
                                             "SELECT SUM(\"size\") FROM RepoSize s "
                                             "LEFT JOIN VirtualRepo v "
                                             "ON s.repo_id=v.repo_id "
                                             "WHERE v.repo_id IS NULL",
                                             get_total_storage_cb,
                                             &size, 0);
    } else {
        ret = syncwerk_server_db_statement_foreach_row (syncw->db,
                                             "SELECT SUM(size) FROM RepoSize s "
                                             "LEFT JOIN VirtualRepo v "
                                             "ON s.repo_id=v.repo_id "
                                             "WHERE v.repo_id IS NULL",
                                             get_total_storage_cb,
                                             &size, 0);
    }
    if (ret < 0) {
        syncw_warning ("Failed to get total storage occupation.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get total storage occupation from db.");
        return -1;
    }

    return size;
}

void
syncw_repo_manager_update_repo_info (SyncwRepoManager *mgr,
                                    const char *repo_id, const char *head_commit_id)
{
    SyncwCommit *head;

    head = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                           repo_id, 1, head_commit_id);
    if (!head) {
        syncw_warning ("Failed to get commit %s:%s.\n", repo_id, head_commit_id);
        return;
    }

    set_repo_commit_to_db (repo_id, head->repo_name, head->ctime, head->version,
                           (head->encrypted ? 1 : 0), head->creator_name);

    syncw_commit_unref (head);
}

char *
syncw_get_trash_repo_owner (const char *repo_id)
{
    char *sql = "SELECT owner_id from RepoTrash WHERE repo_id = ?";
    return syncwerk_server_db_statement_get_string(syncw->db, sql, 1, "string", repo_id);
}

GObject *
syncw_get_group_shared_repo_by_path (SyncwRepoManager *mgr,
                                    const char *repo_id,
                                    const char *path,
                                    int group_id,
                                    gboolean is_org,
                                    GError **error)
{
    char *sql;
    char *real_repo_id = NULL;
    GList *repo = NULL;
    GObject *ret = NULL;

    /* If path is NULL, 'repo_id' represents for the repo we want,
     * otherwise, 'repo_id' represents for the origin repo,
     * find virtual repo by path first.
     */
    if (path != NULL) {
        real_repo_id = syncw_repo_manager_get_virtual_repo_id (mgr, repo_id, path, NULL);
        if (!real_repo_id) {
            syncw_warning ("Failed to get virtual repo_id by path %s, origin_repo: %s\n", path, repo_id);
            return NULL;
        }
    }
    if (!real_repo_id)
        real_repo_id = g_strdup (repo_id);

    if (!is_org)
        sql = "SELECT RepoGroup.repo_id, v.repo_id, "
              "group_id, user_name, permission, commit_id, s.size, "
              "v.origin_repo, v.path, "
              "(SELECT name FROM RepoInfo WHERE repo_id=v.origin_repo) "
              "FROM RepoGroup LEFT JOIN VirtualRepo v ON "
              "RepoGroup.repo_id = v.repo_id "
              "LEFT JOIN RepoSize s ON RepoGroup.repo_id = s.repo_id, "
              "Branch WHERE group_id = ? AND "
              "RepoGroup.repo_id = Branch.repo_id AND "
              "RepoGroup.repo_id = ? AND "
              "Branch.name = 'master'";
    else
        sql = "SELECT OrgGroupRepo.repo_id, v.repo_id, "
              "group_id, owner, permission, commit_id, s.size, "
              "v.origin_repo, v.path, "
              "(SELECT name FROM RepoInfo WHERE repo_id=v.origin_repo) "
              "FROM OrgGroupRepo LEFT JOIN VirtualRepo v ON "
              "OrgGroupRepo.repo_id = v.repo_id "
              "LEFT JOIN RepoSize s ON OrgGroupRepo.repo_id = s.repo_id, "
              "Branch WHERE group_id = ? AND "
              "OrgGroupRepo.repo_id = Branch.repo_id AND "
              "OrgGroupRepo.repo_id = ? AND "
              "Branch.name = 'master'";

    /* The list 'repo' should have only one repo,
     * use existing api get_group_repos_cb() to get it.
     */
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, get_group_repos_cb,
                                       &repo, 2, "int", group_id,
                                       "string", real_repo_id) < 0) {
        g_free (real_repo_id);
        g_list_free (repo);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get repo by group_id from db.");
        return NULL;
    }
    g_free (real_repo_id);

    if (repo) {
        syncw_fill_repo_obj_from_commit (&repo);
        if (repo)
            ret = (GObject *)(repo->data);
        g_list_free (repo);
    }

    return ret;
}

GList *
syncw_get_group_repos_by_user (SyncwRepoManager *mgr,
                              const char *user,
                              int org_id,
                              GError **error)
{
    CcnetGroup *group;
    GList *groups = NULL, *p, *q;
    GList *repos = NULL;
    SyncwerkRepo *repo = NULL;
    RpcsyncwerkClient *rpc_client;
    GString *sql = NULL;
    int group_id = 0;

    rpc_client = ccnet_create_pooled_rpc_client (syncw->client_pool,
                                                 NULL,
                                                 "ccnet-threaded-rpcserver");
    if (!rpc_client)
        return NULL;

    /* Get the groups this user belongs to. */
    groups = ccnet_get_groups_by_user (rpc_client, user, 1);
    if (!groups) {
        goto out;
    }

    sql = g_string_new ("");
    g_string_printf (sql, "SELECT g.repo_id, v.repo_id, "
                          "group_id, %s, permission, commit_id, s.size, "
                          "v.origin_repo, v.path, "
                          "(SELECT name FROM RepoInfo WHERE repo_id=v.origin_repo)"
                          "FROM %s g LEFT JOIN VirtualRepo v ON "
                          "g.repo_id = v.repo_id "
                          "LEFT JOIN RepoSize s ON g.repo_id = s.repo_id, "
                          "Branch b WHERE g.repo_id = b.repo_id AND "
                          "b.name = 'master' AND group_id IN (",
                          org_id < 0 ? "user_name" : "owner",
                          org_id < 0 ? "RepoGroup" : "OrgGroupRepo");
    for (p = groups; p != NULL; p = p->next) {
        group = p->data;
        g_object_get (group, "id", &group_id, NULL);

        g_string_append_printf (sql, "%d", group_id);
        if (p->next)
            g_string_append_printf (sql, ",");
    }
    g_string_append_printf (sql, " ) ORDER BY group_id");

    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql->str, get_group_repos_cb,
                                       &repos, 0) < 0) {
        for (p = repos; p; p = p->next) {
            g_object_unref (p->data);
        }
        g_list_free (repos);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get user group repos from db.");
        syncw_warning ("Failed to get user[%s] group repos from db.\n", user);
        goto out;
    }

    int repo_group_id = 0;
    char *group_name = NULL;
    q = repos;

    /* Add group_name to repo. Both groups and repos are listed by group_id in descending order */
    for (p = groups; p; p = p->next) {
        group = p->data;
        g_object_get (group, "id", &group_id, NULL);
        g_object_get (group, "group_name", &group_name, NULL);

        for (; q; q = q->next) {
            repo = q->data;
            g_object_get (repo, "group_id", &repo_group_id, NULL);
            if (repo_group_id == group_id)
                g_object_set (repo, "group_name", group_name, NULL);
            else
                break;
        }
        g_free (group_name);
        if (q == NULL)
            break;
    }

    syncw_fill_repo_obj_from_commit (&repos);

out:
    if (sql)
        g_string_free (sql, TRUE);

    ccnet_rpc_client_free (rpc_client);
    for (p = groups; p != NULL; p = p->next)
        g_object_unref ((GObject *)p->data);
    g_list_free (groups);

    return g_list_reverse (repos);
}

typedef struct RepoPath {
    char *repo_id;
    char *path;
    int group_id;
} RepoPath;


gboolean
convert_repo_path_cb (SyncwDBRow *row, void *data)
{
    GList **repo_paths = data;

    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    const char *path = syncwerk_server_db_row_get_column_text (row, 1);
    int group_id = syncwerk_server_db_row_get_column_int (row, 2);

    RepoPath *rp = g_new0(RepoPath, 1);
    rp->repo_id = g_strdup(repo_id);
    rp->path = g_strdup(path);
    rp->group_id = group_id;
    *repo_paths = g_list_append (*repo_paths, rp);

    return TRUE;
}

static void
free_repo_path (gpointer data)
{
    if (!data)
        return;

    RepoPath *rp = data;
    g_free (rp->repo_id);
    g_free (rp->path);
    g_free (rp);
}

static char *
filter_path (GList *repo_paths, const char *path)
{
    GList *ptr = NULL;
    int len;
    const char *relative_path;
    char *ret = NULL;
    RepoPath *rp = NULL, res;
    res.repo_id = NULL;
    res.path = NULL;
    res.group_id = 0;

    /* Find nearest item which contains @path, */
    for (ptr = repo_paths; ptr; ptr = ptr->next) {
        rp = ptr->data;
        len = strlen(rp->path);
        if (strncmp(rp->path, path, len) == 0 && (path[len] == '/' || path[len] == '\0')) {

            if (g_strcmp0(rp->path, res.path) > 0) {
                res.path = rp->path;
                res.repo_id = rp->repo_id;
                res.group_id = rp->group_id;
            }
        }
    }
    if (res.repo_id && res.path) {
        relative_path = path + strlen(res.path);
        if (relative_path[0] == '\0')
            relative_path = "/";

        json_t *json = json_object ();
        json_object_set_string_member(json, "repo_id", res.repo_id);
        json_object_set_string_member(json, "path", relative_path);
        if (res.group_id > 0)
            json_object_set_int_member(json, "group_id", res.group_id);
        ret = json_dumps (json, 0);
        json_decref (json);
    }

    return ret;
}

/* Convert origin repo and path to virtual repo and relative path */
char *
syncw_repo_manager_convert_repo_path (SyncwRepoManager *mgr,
                                     const char *repo_id,
                                     const char *path,
                                     const char *user,
                                     gboolean is_org,
                                     GError **error)
{
    char *ret = NULL;
    int rc;
    int group_id;
    GString *sql;
    RpcsyncwerkClient *rpc_client = NULL;
    CcnetGroup *group;
    GList *groups = NULL, *p1;
    GList *repo_paths = NULL;
    SyncwVirtRepo *vinfo = NULL;
    const char *r_repo_id = repo_id;
    char *r_path = NULL;

    vinfo = syncw_repo_manager_get_virtual_repo_info (mgr, repo_id);
    if (vinfo) {
        r_repo_id = vinfo->origin_repo_id;
        r_path = g_strconcat (vinfo->path, path, NULL);
    } else {
        r_path = g_strdup(path);
    }

    sql = g_string_new ("");
    g_string_printf (sql, "SELECT v.repo_id, path, 0 FROM VirtualRepo v, %s s WHERE "
                     "v.origin_repo=? AND v.repo_id=s.repo_id AND s.to_email=?",
                     is_org ? "OrgSharedRepo" : "SharedRepo");
    rc = syncwerk_server_db_statement_foreach_row (syncw->db,
                                        sql->str, convert_repo_path_cb,
                                        &repo_paths, 2,
                                        "string", r_repo_id, "string", user);
    if (rc < 0) {
        syncw_warning("Failed to convert repo path [%s:%s] to virtual repo path, db_error.\n",
                     repo_id, path);
        goto out;
    }
    ret = filter_path(repo_paths, r_path);
    g_list_free_full(repo_paths, free_repo_path);
    repo_paths = NULL;
    if (ret)
        goto out;

    rpc_client = ccnet_create_pooled_rpc_client (syncw->client_pool,
                                                 NULL,
                                                 "ccnet-threaded-rpcserver");
    if (!rpc_client)
        goto out;

    /* Get the groups this user belongs to. */
    groups = ccnet_get_groups_by_user (rpc_client, user, 1);
    if (!groups) {
        goto out;
    }

    g_string_printf (sql, "SELECT v.repo_id, path, r.group_id FROM VirtualRepo v, %s r WHERE "
                     "v.origin_repo=? AND v.repo_id=r.repo_id AND r.group_id IN(",
                     is_org ? "OrgGroupRepo" : "RepoGroup");
    for (p1 = groups; p1 != NULL; p1 = p1->next) {
        group = p1->data;
        g_object_get (group, "id", &group_id, NULL);

        g_string_append_printf (sql, "%d", group_id);
        if (p1->next)
            g_string_append_printf (sql, ",");
    }
    g_string_append_printf (sql, ")");

    rc = syncwerk_server_db_statement_foreach_row (syncw->db,
                                        sql->str, convert_repo_path_cb,
                                        &repo_paths, 1,
                                        "string", r_repo_id);
    if (rc < 0) {
        syncw_warning("Failed to convert repo path [%s:%s] to virtual repo path, db error.\n",
                     repo_id, path);
        g_string_free (sql, TRUE);
        goto out;
    }
    ret = filter_path(repo_paths, r_path);
    g_list_free_full(repo_paths, free_repo_path);

out:
    g_free (r_path);
    if (vinfo)
        syncw_virtual_repo_info_free (vinfo);
    g_string_free (sql, TRUE);
    ccnet_rpc_client_free (rpc_client);
    for (p1 = groups; p1 != NULL; p1 = p1->next)
        g_object_unref ((GObject *)p1->data);
    g_list_free (groups);

    return ret;
}

