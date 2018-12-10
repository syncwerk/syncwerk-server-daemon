/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"
#include <glib/gstdio.h>

#include <ccnet.h>
#include "utils.h"
#include "log.h"

#include "syncwerk-session.h"
#include "commit-mgr.h"
#include "branch-mgr.h"
#include "repo-mgr.h"
#include "fs-mgr.h"
#include "syncwerk-error.h"

#include "syncwerk-server-db.h"

#define INDEX_DIR "index"

struct _SyncwRepoManagerPriv {

};

static SyncwRepo *
load_repo (SyncwRepoManager *manager, const char *repo_id);

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
    if (repo->category) g_free (repo->category);
    if (repo->head) syncw_branch_unref (repo->head);
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

void
syncw_repo_from_commit (SyncwRepo *repo, SyncwCommit *commit)
{
    repo->name = g_strdup (commit->repo_name);
    repo->desc = g_strdup (commit->repo_desc);
    repo->encrypted = commit->encrypted;
    if (repo->encrypted) {
        repo->enc_version = commit->enc_version;
        if (repo->enc_version >= 1)
            memcpy (repo->magic, commit->magic, 33);
    }
    repo->no_local_history = commit->no_local_history;
    repo->version = commit->version;
}

void
syncw_repo_to_commit (SyncwRepo *repo, SyncwCommit *commit)
{
    commit->repo_name = g_strdup (repo->name);
    commit->repo_desc = g_strdup (repo->desc);
    commit->encrypted = repo->encrypted;
    if (commit->encrypted) {
        commit->enc_version = repo->enc_version;
        if (commit->enc_version >= 1)
            commit->magic = g_strdup (repo->magic);
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

static int 
compare_repo (const SyncwRepo *srepo, const SyncwRepo *trepo)
{
    return g_strcmp0 (srepo->id, trepo->id);
}

SyncwRepoManager*
syncw_repo_manager_new (SyncwerkSession *syncw)
{
    SyncwRepoManager *mgr = g_new0 (SyncwRepoManager, 1);

    mgr->priv = g_new0 (SyncwRepoManagerPriv, 1);
    mgr->syncw = syncw;

    return mgr;
}

int
syncw_repo_manager_init (SyncwRepoManager *mgr)
{
    return 0;
}

int
syncw_repo_manager_start (SyncwRepoManager *mgr)
{
    return 0;
}

static gboolean
repo_exists_in_db (SyncwDB *db, const char *id)
{
    char sql[256];
    gboolean db_err = FALSE;

    snprintf (sql, sizeof(sql), "SELECT repo_id FROM Repo WHERE repo_id = '%s'",
              id);
    return syncwerk_server_db_check_for_existence (db, sql, &db_err);
}

SyncwRepo*
syncw_repo_manager_get_repo (SyncwRepoManager *manager, const gchar *id)
{
    SyncwRepo repo;
    int len = strlen(id);

    if (len >= 37)
        return NULL;

    memcpy (repo.id, id, len + 1);

    if (repo_exists_in_db (manager->syncw->db, id)) {
        SyncwRepo *ret = load_repo (manager, id);
        if (!ret)
            return NULL;
        /* syncw_repo_ref (ret); */
        return ret;
    }

    return NULL;
}

gboolean
syncw_repo_manager_repo_exists (SyncwRepoManager *manager, const gchar *id)
{
    SyncwRepo repo;
    memcpy (repo.id, id, 37);

    return repo_exists_in_db (manager->syncw->db, id);
}

static void
load_repo_commit (SyncwRepoManager *manager,
                  SyncwRepo *repo,
                  SyncwBranch *branch)
{
    SyncwCommit *commit;

    commit = syncw_commit_manager_get_commit_compatible (manager->syncw->commit_mgr,
                                                        repo->id,
                                                        branch->commit_id);
    if (!commit) {
        syncw_warning ("Commit %s is missing\n", branch->commit_id);
        repo->is_corrupted = TRUE;
        return;
    }

    set_head_common (repo, branch);
    syncw_repo_from_commit (repo, commit);

    syncw_commit_unref (commit);
}

static gboolean
load_virtual_info (SyncwDBRow *row, void *vrepo_id)
{
    char *ret_repo_id = vrepo_id;
    const char *origin_repo_id;

    origin_repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    memcpy (ret_repo_id, origin_repo_id, 37);

    return FALSE;
}

char *
get_origin_repo_id (SyncwRepoManager *mgr, const char *repo_id)
{
    char sql[256];
    char origin_repo_id[37];

    memset (origin_repo_id, 0, 37);

    snprintf (sql, 256,
              "SELECT origin_repo FROM VirtualRepo "
              "WHERE repo_id = '%s'", repo_id);
    syncwerk_server_db_foreach_selected_row (syncw->db, sql, load_virtual_info, origin_repo_id);

    if (origin_repo_id[0] != 0)
        return g_strdup(origin_repo_id);
    else
        return NULL;
}

static SyncwRepo *
load_repo (SyncwRepoManager *manager, const char *repo_id)
{
    SyncwRepo *repo;
    SyncwBranch *branch;

    repo = syncw_repo_new(repo_id, NULL, NULL);
    if (!repo) {
        syncw_warning ("[repo mgr] failed to alloc repo.\n");
        return NULL;
    }

    repo->manager = manager;

    branch = syncw_branch_manager_get_branch (syncw->branch_mgr, repo_id, "master");
    if (!branch) {
        syncw_warning ("Failed to get master branch of repo %.8s.\n", repo_id);
        repo->is_corrupted = TRUE;
    } else {
        load_repo_commit (manager, repo, branch);
        syncw_branch_unref (branch);
    }

    if (repo->is_corrupted) {
        syncw_warning ("Repo %.8s is corrupted.\n", repo->id);
        syncw_repo_free (repo);
        return NULL;
    }

    char *origin_repo_id = get_origin_repo_id (manager, repo->id);
    if (origin_repo_id)
        memcpy (repo->store_id, origin_repo_id, 36);
    else
        memcpy (repo->store_id, repo->id, 36);
    g_free (origin_repo_id);

    return repo;
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

GList *
syncw_repo_manager_get_repo_list (SyncwRepoManager *mgr, int start, int limit)
{
    GList *id_list = NULL, *ptr;
    GList *ret = NULL;
    SyncwRepo *repo;
    char sql[256];

    if (start == -1 && limit == -1)
        snprintf (sql, 256, "SELECT repo_id FROM Repo");
    else
        snprintf (sql, 256, "SELECT repo_id FROM Repo LIMIT %d, %d", start, limit);

    if (syncwerk_server_db_foreach_selected_row (mgr->syncw->db, sql, 
                                      collect_repo_id, &id_list) < 0)
        return NULL;

    for (ptr = id_list; ptr; ptr = ptr->next) {
        char *repo_id = ptr->data;
        repo = syncw_repo_manager_get_repo (mgr, repo_id);
        if (repo != NULL)
            ret = g_list_prepend (ret, repo);
    }

    string_list_free (id_list);
    return g_list_reverse (ret);
}

GList *
syncw_repo_manager_get_repos_by_owner (SyncwRepoManager *mgr,
                                      const char *email)
{
    GList *id_list = NULL, *ptr;
    GList *ret = NULL;
    char sql[256];

    snprintf (sql, 256, "SELECT repo_id FROM RepoOwner WHERE owner_id='%s'",
              email);

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
syncw_repo_manager_is_virtual_repo (SyncwRepoManager *mgr, const char *repo_id)
{
    char sql[256];
    gboolean db_err;

    snprintf (sql, 256,
              "SELECT 1 FROM VirtualRepo WHERE repo_id = '%s'", repo_id);
    return syncwerk_server_db_check_for_existence (syncw->db, sql, &db_err);
}
