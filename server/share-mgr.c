/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"
#include "utils.h"

#include "log.h"

#include "syncwerk-session.h"
#include "share-mgr.h"

#include "syncwerk-server-db.h"
#include "log.h"
#include "syncwerk-error.h"

SyncwShareManager *
syncw_share_manager_new (SyncwerkSession *syncw)
{
    SyncwShareManager *mgr = g_new0 (SyncwShareManager, 1);

    mgr->syncw = syncw;

    return mgr;
}

int
syncw_share_manager_start (SyncwShareManager *mgr)
{
    if (!mgr->syncw->create_tables && syncwerk_server_db_type (mgr->syncw->db) == SYNCW_DB_TYPE_MYSQL)
        return 0;

    SyncwDB *db = mgr->syncw->db;
    const char *sql;

    int db_type = syncwerk_server_db_type (db);
    if (db_type == SYNCW_DB_TYPE_MYSQL) {
        sql = "CREATE TABLE IF NOT EXISTS SharedRepo "
            "(id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT,"
            "repo_id CHAR(37) , from_email VARCHAR(255), to_email VARCHAR(255), "
            "permission CHAR(15), INDEX (repo_id), "
            "INDEX(from_email), INDEX(to_email)) ENGINE=INNODB";

        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
    } else if (db_type == SYNCW_DB_TYPE_SQLITE) {
        sql = "CREATE TABLE IF NOT EXISTS SharedRepo "
            "(repo_id CHAR(37) , from_email VARCHAR(255), to_email VARCHAR(255), "
            "permission CHAR(15))";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
        sql = "CREATE INDEX IF NOT EXISTS RepoIdIndex on SharedRepo (repo_id)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
        sql = "CREATE INDEX IF NOT EXISTS FromEmailIndex on SharedRepo (from_email)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
        sql = "CREATE INDEX IF NOT EXISTS ToEmailIndex on SharedRepo (to_email)";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;
    } else if (db_type == SYNCW_DB_TYPE_PGSQL) {
        sql = "CREATE TABLE IF NOT EXISTS SharedRepo "
            "(repo_id CHAR(36) , from_email VARCHAR(255), to_email VARCHAR(255), "
            "permission VARCHAR(15))";
        if (syncwerk_server_db_query (db, sql) < 0)
            return -1;

        if (!pgsql_index_exists (db, "sharedrepo_repoid_idx")) {
            sql = "CREATE INDEX sharedrepo_repoid_idx ON SharedRepo (repo_id)";
            if (syncwerk_server_db_query (db, sql) < 0)
                return -1;
        }
        if (!pgsql_index_exists (db, "sharedrepo_from_email_idx")) {
            sql = "CREATE INDEX sharedrepo_from_email_idx ON SharedRepo (from_email)";
            if (syncwerk_server_db_query (db, sql) < 0)
                return -1;
        }
        if (!pgsql_index_exists (db, "sharedrepo_to_email_idx")) {
            sql = "CREATE INDEX sharedrepo_to_email_idx ON SharedRepo (to_email)";
            if (syncwerk_server_db_query (db, sql) < 0)
                return -1;
        }
    }
    
    return 0;
}

int
syncw_share_manager_add_share (SyncwShareManager *mgr, const char *repo_id,
                              const char *from_email, const char *to_email,
                              const char *permission)
{
    gboolean db_err = FALSE;
    int ret = 0;

    char *from_email_l = g_ascii_strdown (from_email, -1);
    char *to_email_l = g_ascii_strdown (to_email, -1);

    if (syncwerk_server_db_statement_exists (mgr->syncw->db,
                                  "SELECT repo_id from SharedRepo "
                                  "WHERE repo_id=? AND "
                                  "from_email=? AND to_email=?",
                                  &db_err, 3, "string", repo_id,
                                  "string", from_email_l, "string", to_email_l))
        goto out;

    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                                 "INSERT INTO SharedRepo (repo_id, from_email, "
                                 "to_email, permission) VALUES (?, ?, ?, ?)",
                                 4, "string", repo_id, "string", from_email_l,
                                 "string", to_email_l, "string", permission) < 0) {
        ret = -1;
        goto out;
    }

out:
    g_free (from_email_l);
    g_free (to_email_l);
    return ret;
}

int
syncw_share_manager_set_subdir_perm_by_path (SyncwShareManager *mgr, const char *repo_id,
                                           const char *from_email, const char *to_email,
                                           const char *permission, const char *path)
{
    char *sql;
    int ret;

    char *from_email_l = g_ascii_strdown (from_email, -1);
    char *to_email_l = g_ascii_strdown (to_email, -1);
    sql = "UPDATE SharedRepo SET permission=? WHERE repo_id IN "
          "(SELECT repo_id FROM VirtualRepo WHERE origin_repo=? AND path=?) "
          "AND from_email=? AND to_email=?";

    ret = syncwerk_server_db_statement_query (mgr->syncw->db, sql,
                                   5, "string", permission,
                                   "string", repo_id,
                                   "string", path,
                                   "string", from_email_l,
                                   "string", to_email_l);
    g_free (from_email_l);
    g_free (to_email_l);
    return ret;
}

int
syncw_share_manager_set_permission (SyncwShareManager *mgr, const char *repo_id,
                                   const char *from_email, const char *to_email,
                                   const char *permission)
{
    char *sql;
    int ret;

    char *from_email_l = g_ascii_strdown (from_email, -1);
    char *to_email_l = g_ascii_strdown (to_email, -1);
    sql = "UPDATE SharedRepo SET permission=? WHERE "
        "repo_id=? AND from_email=? AND to_email=?";

    ret = syncwerk_server_db_statement_query (mgr->syncw->db, sql,
                                   4, "string", permission, "string", repo_id,
                                   "string", from_email_l, "string", to_email_l);

    g_free (from_email_l);
    g_free (to_email_l);
    return ret;
}

static gboolean
collect_repos (SyncwDBRow *row, void *data)
{
    GList **p_repos = data;
    const char *repo_id;
    const char *vrepo_id;
    const char *email;
    const char *permission;
    const char *commit_id;
    gint64 size;
    SyncwerkRepo *repo;

    repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    vrepo_id = syncwerk_server_db_row_get_column_text (row, 1);
    email = syncwerk_server_db_row_get_column_text (row, 2);
    permission = syncwerk_server_db_row_get_column_text (row, 3);
    commit_id = syncwerk_server_db_row_get_column_text (row, 4);
    size = syncwerk_server_db_row_get_column_int64 (row, 5);
    const char *repo_name = syncwerk_server_db_row_get_column_text (row, 8);
    gint64 update_time = syncwerk_server_db_row_get_column_int64 (row, 9);
    int version = syncwerk_server_db_row_get_column_int (row, 10); 
    gboolean is_encrypted = syncwerk_server_db_row_get_column_int (row, 11) ? TRUE : FALSE;
    const char *last_modifier = syncwerk_server_db_row_get_column_text (row, 12);
    const char *origin_repo_name = syncwerk_server_db_row_get_column_text (row, 13);

    char *email_l = g_ascii_strdown (email, -1);

    repo = g_object_new (SYNCWERK_TYPE_REPO,
                         "share_type", "personal",
                         "repo_id", repo_id,
                         "id", repo_id,
                         "head_cmmt_id", commit_id,
                         "user", email_l,
                         "permission", permission,
                         "is_virtual", (vrepo_id != NULL),
                         "size", size,
                         NULL);
    g_free (email_l);

    if (repo) {
        if (vrepo_id) {
            const char *origin_repo_id = syncwerk_server_db_row_get_column_text (row, 6);
            const char *origin_path = syncwerk_server_db_row_get_column_text (row, 7);
            g_object_set (repo, "store_id", origin_repo_id,
                          "origin_repo_id", origin_repo_id,
                          "origin_repo_name", origin_repo_name,
                          "origin_path", origin_path, NULL);
        } else {
            g_object_set (repo, "store_id", repo_id, NULL);
        }
        if (repo_name) {
            g_object_set (repo, "name", repo_name,
                          "repo_name", repo_name,
                          "last_modify", update_time,
                          "last_modified", update_time,
                          "version", version,
                          "encrypted", is_encrypted,
                          "last_modifier", last_modifier, NULL);
        }
        *p_repos = g_list_prepend (*p_repos, repo);
    }

    return TRUE;
}

static void
syncw_fill_repo_commit_if_not_in_db (GList **repos)
{
    char *repo_name = NULL;
    char *last_modifier = NULL;
    char *repo_id = NULL;
    char *commit_id = NULL;
    SyncwerkRepo *repo = NULL;
    GList *p = NULL;

    for (p = *repos; p;) {
        repo = p->data;
        g_object_get (repo, "name", &repo_name, NULL);
        g_object_get (repo, "last_modifier", &last_modifier, NULL);
        if (!repo_name || !last_modifier) {
            g_object_get (repo, "repo_id", &repo_id,
                          "head_cmmt_id", &commit_id, NULL);
            SyncwCommit *commit = syncw_commit_manager_get_commit_compatible (syncw->commit_mgr,
                                                                            repo_id, commit_id);
            if (!commit) {
                syncw_warning ("Commit %s:%s is missing\n", repo_id, commit_id);
                GList *next = p->next;
                g_object_unref (repo);
                *repos = g_list_delete_link (*repos, p);
                p = next;
                if (repo_name)
                    g_free (repo_name);
                if (last_modifier)
                    g_free (last_modifier);
                continue;
            } else {
                g_object_set (repo, "name", commit->repo_name,
                                    "repo_name", commit->repo_name,
                                    "last_modify", commit->ctime,
                                    "last_modified", commit->ctime,
                                    "version", commit->version,
                                    "encrypted", commit->encrypted,
                                    "last_modifier", commit->creator_name,
                                    NULL);

                /* Set to database */
                set_repo_commit_to_db (repo_id, commit->repo_name, commit->ctime, commit->version,
                                       commit->encrypted, commit->creator_name);

                syncw_commit_unref (commit);
            }
            g_free (repo_id);
            g_free (commit_id);
        }
        if (repo_name)
            g_free (repo_name);
        if (last_modifier)
            g_free (last_modifier);

        p = p->next;
    }
}

GList*
syncw_share_manager_list_share_repos (SyncwShareManager *mgr, const char *email,
                                     const char *type, int start, int limit)
{
    GList *ret = NULL, *p;
    char *sql;

    if (start == -1 && limit == -1) {
        if (g_strcmp0 (type, "from_email") == 0) {
            sql = "SELECT sh.repo_id, v.repo_id, "
                "to_email, permission, commit_id, s.size, "
                "v.origin_repo, v.path, i.name, "
                "i.update_time, i.version, i.is_encrypted, i.last_modifier, "
                "(SELECT name from RepoInfo WHERE repo_id=v.origin_repo) FROM "
                "SharedRepo sh LEFT JOIN VirtualRepo v ON "
                "sh.repo_id=v.repo_id "
                "LEFT JOIN RepoSize s ON sh.repo_id = s.repo_id "
                "LEFT JOIN RepoInfo i ON sh.repo_id = i.repo_id, Branch b "
                "WHERE from_email=? AND "
                "sh.repo_id = b.repo_id AND "
                "b.name = 'master' "
                "ORDER BY i.update_time DESC, sh.repo_id";
        } else if (g_strcmp0 (type, "to_email") == 0) {
            sql = "SELECT sh.repo_id, v.repo_id, "
                "from_email, permission, commit_id, s.size, "
                "v.origin_repo, v.path, i.name, "
                "i.update_time, i.version, i.is_encrypted, i.last_modifier,"
                "(SELECT name from RepoInfo WHERE repo_id=v.origin_repo) FROM "
                "SharedRepo sh LEFT JOIN VirtualRepo v ON "
                "sh.repo_id=v.repo_id "
                "LEFT JOIN RepoSize s ON sh.repo_id = s.repo_id "
                "LEFT JOIN RepoInfo i ON sh.repo_id = i.repo_id, Branch b "
                "WHERE to_email=? AND "
                "sh.repo_id = b.repo_id AND "
                "b.name = 'master' "
                "ORDER BY i.update_time DESC, sh.repo_id";
        } else {
            /* should never reach here */
            syncw_warning ("[share mgr] Wrong column type");
            return NULL;
        }

        if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                           collect_repos, &ret,
                                           1, "string", email) < 0) {
            syncw_warning ("[share mgr] DB error when get shared repo id and email "
                       "for %s.\n", email);
            for (p = ret; p; p = p->next)
                g_object_unref (p->data);
            g_list_free (ret);
            return NULL;
        }
    }
    else {
        if (g_strcmp0 (type, "from_email") == 0) {
            sql = "SELECT sh.repo_id, v.repo_id, "
                "to_email, permission, commit_id, s.size, "
                "v.origin_repo, v.path, i.name, "
                "i.update_time, i.version, i.is_encrypted, i.last_modifier,"
                "(SELECT name from RepoInfo WHERE repo_id=v.origin_repo) FROM "
                "SharedRepo sh LEFT JOIN VirtualRepo v ON "
                "sh.repo_id=v.repo_id "
                "LEFT JOIN RepoSize s ON sh.repo_id = s.repo_id "
                "LEFT JOIN RepoInfo i ON sh.repo_id = i.repo_id, Branch b "
                "WHERE from_email=? "
                "AND sh.repo_id = b.repo_id "
                "AND b.name = 'master' "
                "ORDER BY i.update_time DESC, sh.repo_id "
                "LIMIT ? OFFSET ?";
        } else if (g_strcmp0 (type, "to_email") == 0) {
            sql = "SELECT sh.repo_id, v.repo_id, "
                "from_email, permission, commit_id, s.size, "
                "v.origin_repo, v.path, i.name, "
                "i.update_time, i.version, i.is_encrypted, i.last_modifier,"
                "(SELECT name from RepoInfo WHERE repo_id=v.origin_repo) FROM "
                "SharedRepo sh LEFT JOIN VirtualRepo v ON "
                "sh.repo_id=v.repo_id "
                "LEFT JOIN RepoSize s ON sh.repo_id = s.repo_id "
                "LEFT JOIN RepoInfo i ON sh.repo_id = i.repo_id, Branch b "
                "WHERE to_email=? "
                "AND sh.repo_id = b.repo_id "
                "AND b.name = 'master' "
                "ORDER BY i.update_time DESC, sh.repo_id "
                "LIMIT ? OFFSET ?";
        } else {
            /* should never reach here */
            syncw_warning ("[share mgr] Wrong column type");
            return NULL;
        }

        if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                           collect_repos, &ret,
                                           3, "string", email,
                                           "int", limit, "int", start) < 0) {
            syncw_warning ("[share mgr] DB error when get shared repo id and email "
                       "for %s.\n", email);
            for (p = ret; p; p = p->next)
                g_object_unref (p->data);
            g_list_free (ret);
            return NULL;
        }
    }

    syncw_fill_repo_commit_if_not_in_db (&ret);

    return g_list_reverse (ret);
}

static gboolean
collect_shared_to (SyncwDBRow *row, void *data)
{
    GList **plist = data;
    const char *to_email;

    to_email = syncwerk_server_db_row_get_column_text (row, 0);
    *plist = g_list_prepend (*plist, g_ascii_strdown(to_email, -1));

    return TRUE;
}

GList *
syncw_share_manager_list_shared_to (SyncwShareManager *mgr,
                                   const char *owner,
                                   const char *repo_id)
{
    char *sql;
    GList *ret = NULL;

    sql = "SELECT to_email FROM SharedRepo WHERE "
        "from_email=? AND repo_id=?";
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                       collect_shared_to, &ret,
                                       2, "string", owner, "string", repo_id) < 0) {
        syncw_warning ("[share mgr] DB error when list shared to.\n");
        string_list_free (ret);
        return NULL;
    }

    return ret;
}

static gboolean
collect_repo_shared_to (SyncwDBRow *row, void *data)
{
    GList **shared_to = data;
    const char *to_email = syncwerk_server_db_row_get_column_text (row, 0);
    char *email_down = g_ascii_strdown(to_email, -1);
    const char *perm = syncwerk_server_db_row_get_column_text (row, 1);
    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 2);

    SyncwerkSharedUser *uobj = g_object_new (SYNCWERK_TYPE_SHARED_USER,
                                            "repo_id", repo_id,
                                            "user", email_down,
                                            "perm", perm,
                                            NULL);
    *shared_to = g_list_prepend (*shared_to, uobj);
    g_free (email_down);

    return TRUE;
}

GList *
syncw_share_manager_list_repo_shared_to (SyncwShareManager *mgr,
                                        const char *from_email,
                                        const char *repo_id,
                                        GError **error)
{
    GList *shared_to = NULL;
    char *sql = "SELECT to_email, permission, repo_id FROM SharedRepo WHERE "
                "from_email=? AND repo_id=?";

    int ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                             collect_repo_shared_to, &shared_to,
                                             2, "string", from_email, "string", repo_id);
    if (ret < 0) {
        syncw_warning ("Failed to list repo %s shared to from db.\n", repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to list repo shared to from db");
        while (shared_to) {
            g_object_unref (shared_to->data);
            shared_to = g_list_delete_link (shared_to, shared_to);
        }
        return NULL;
    }

    return shared_to;
}

static gboolean
collect_repo_shared_group (SyncwDBRow *row, void *data)
{
    GList **shared_group = data;
    int group_id = syncwerk_server_db_row_get_column_int (row, 0);
    const char *perm = syncwerk_server_db_row_get_column_text (row, 1);
    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 2);

    SyncwerkSharedGroup *gobj = g_object_new (SYNCWERK_TYPE_SHARED_GROUP,
                                             "repo_id", repo_id,
                                             "group_id", group_id,
                                             "perm", perm,
                                             NULL);
    *shared_group = g_list_prepend (*shared_group, gobj);

    return TRUE;
}

GList *
syncw_share_manager_list_repo_shared_group (SyncwShareManager *mgr,
                                           const char *from_email,
                                           const char *repo_id,
                                           GError **error)
{
    GList *shared_group = NULL;
    char *sql = "SELECT group_id, permission, repo_id FROM RepoGroup WHERE "
                "user_name=? AND repo_id=?";

    int ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                             collect_repo_shared_group, &shared_group,
                                             2, "string", from_email, "string", repo_id);
    if (ret < 0) {
        syncw_warning ("Failed to list repo %s shared group from db.\n", repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to list repo shared group from db");
        while (shared_group) {
            g_object_unref (shared_group->data);
            shared_group = g_list_delete_link (shared_group, shared_group);
        }
        return NULL;
    }

    return shared_group;
}

int
syncw_share_manager_remove_share (SyncwShareManager *mgr, const char *repo_id,
                                 const char *from_email, const char *to_email)
{
    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                       "DELETE FROM SharedRepo WHERE repo_id = ? AND from_email ="
                       " ? AND to_email = ?",
                       3, "string", repo_id, "string", from_email,
                       "string", to_email) < 0)
        return -1;

    return 0;
}

int
syncw_share_manager_unshare_subdir (SyncwShareManager* mgr,
                                   const char *orig_repo_id,
                                   const char *path,
                                   const char *from_email,
                                   const char *to_email)
{
    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                                 "DELETE FROM SharedRepo WHERE "
                                 "from_email = ? AND to_email = ? "
                                 "AND repo_id IN "
                                 "(SELECT repo_id FROM VirtualRepo WHERE "
                                 "origin_repo = ? AND path = ?)",
                                 4, "string", from_email,
                                 "string", to_email,
                                 "string", orig_repo_id,
                                 "string", path) < 0)
        return -1;

    return 0;
}

int
syncw_share_manager_remove_repo (SyncwShareManager *mgr, const char *repo_id)
{
    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                       "DELETE FROM SharedRepo WHERE repo_id = ?",
                       1, "string", repo_id) < 0)
        return -1;

    return 0;
}

char *
syncw_share_manager_check_permission (SyncwShareManager *mgr,
                                     const char *repo_id,
                                     const char *email)
{
    char *sql;

    sql = "SELECT permission FROM SharedRepo WHERE repo_id=? AND to_email=?";
    return syncwerk_server_db_statement_get_string (mgr->syncw->db, sql,
                                         2, "string", repo_id, "string", email);
}

static gboolean
get_shared_sub_dirs (SyncwDBRow *row, void *data)
{
    GHashTable *sub_dirs = data;
    int dummy;

    const char *sub_dir = syncwerk_server_db_row_get_column_text (row, 0);
    g_hash_table_replace (sub_dirs, g_strdup(sub_dir), &dummy);

    return TRUE;
}

GHashTable *
syncw_share_manager_get_shared_sub_dirs (SyncwShareManager *mgr,
                                        const char *repo_id,
                                        const char *path)
{
    GHashTable *sub_dirs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, NULL);
    char *pattern;
    if (strcmp (path, "/") == 0) {
        pattern = g_strdup_printf("%s%%", path);
    } else {
        pattern = g_strdup_printf ("%s/%%", path);
    }
    int ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db,
                                             "SELECT v.path FROM VirtualRepo v, SharedRepo s "
                                             "WHERE v.repo_id = s.repo_id and "
                                             "v.origin_repo = ? AND v.path LIKE ?",
                                             get_shared_sub_dirs, sub_dirs,
                                             2, "string", repo_id, "string", pattern);

    if (ret < 0) {
        g_free (pattern);
        syncw_warning ("Failed to get shared sub dirs from db.\n");
        g_hash_table_destroy (sub_dirs);
        return NULL;
    }

    ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db,
                                         "SELECT v.path FROM VirtualRepo v, RepoGroup r "
                                         "WHERE v.repo_id = r.repo_id and "
                                         "v.origin_repo = ? AND v.path LIKE ?",
                                         get_shared_sub_dirs, sub_dirs,
                                         2, "string", repo_id, "string", pattern);
    g_free (pattern);

    if (ret < 0) {
        syncw_warning ("Failed to get shared sub dirs from db.\n");
        g_hash_table_destroy (sub_dirs);
        return NULL;
    }

    return sub_dirs;
}

int
syncw_share_manager_is_repo_shared (SyncwShareManager *mgr,
                                   const char *repo_id)
{
    gboolean ret;
    gboolean db_err = FALSE;

    ret = syncwerk_server_db_statement_exists (mgr->syncw->db,
                                    "SELECT repo_id FROM SharedRepo WHERE "
                                    "repo_id = ?", &db_err,
                                    1, "string", repo_id);
    if (db_err) {
        syncw_warning ("DB error when check repo exist in SharedRepo.\n");
        return -1;
    }

    if (!ret) {
        ret = syncwerk_server_db_statement_exists (mgr->syncw->db,
                                        "SELECT repo_id FROM RepoGroup WHERE "
                                        "repo_id = ?", &db_err,
                                        1, "string", repo_id);
        if (db_err) {
            syncw_warning ("DB error when check repo exist in RepoGroup.\n");
            return -1;
        }
    }

    return ret;
}

GObject *
syncw_get_shared_repo_by_path (SyncwRepoManager *mgr,
                              const char *repo_id,
                              const char *path,
                              const char *shared_to,
                              int is_org,
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
        sql = "SELECT sh.repo_id, v.repo_id, "
              "from_email, permission, commit_id, s.size, "
              "v.origin_repo, v.path, i.name, "
              "i.update_time, i.version, i.is_encrypted, i.last_modifier,"
              "(SELECT name from RepoInfo WHERE repo_id=v.origin_repo) FROM "
              "SharedRepo sh LEFT JOIN VirtualRepo v ON "
              "sh.repo_id=v.repo_id "
              "LEFT JOIN RepoSize s ON sh.repo_id = s.repo_id "
              "LEFT JOIN RepoInfo i ON sh.repo_id = i.repo_id, Branch b "
              "WHERE to_email=? AND "
              "sh.repo_id = b.repo_id AND sh.repo_id=? AND "
              "b.name = 'master' ";
    else
        sql = "SELECT sh.repo_id, v.repo_id, "
              "from_email, permission, commit_id, s.size, "
              "v.origin_repo, v.path, i.name, "
              "i.update_time, i.version, i.is_encrypted, i.last_modifier,"
              "(SELECT name from RepoInfo WHERE repo_id=v.origin_repo) FROM "
              "OrgSharedRepo sh LEFT JOIN VirtualRepo v ON "
              "sh.repo_id=v.repo_id "
              "LEFT JOIN RepoSize s ON sh.repo_id = s.repo_id "
              "LEFT JOIN RepoInfo i ON sh.repo_id = i.repo_id, Branch b "
              "WHERE to_email=? AND "
              "sh.repo_id = b.repo_id AND sh.repo_id=? AND "
              "b.name = 'master' ";

    /* The list 'repo' should have only one repo,
     * use existing api collect_repos() to get it.
     */
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                       collect_repos, &repo,
                                       2, "string", shared_to, "string", real_repo_id) < 0) {
            g_free (real_repo_id);
            g_list_free (repo);
            syncw_warning ("[share mgr] DB error when get shared repo "
                          "for %s, path:%s\n", shared_to, path);
            return NULL;
    }
    g_free (real_repo_id);
    if (repo) {
        ret = (GObject *)(repo->data);
        g_list_free (repo);
    }

    return ret;
}

int
syncw_share_manager_unshare_group_subdir (SyncwShareManager* mgr,
                                         const char *repo_id,
                                         const char *path,
                                         const char *owner,
                                         int group_id)
{
    if (syncwerk_server_db_statement_query (mgr->syncw->db,
                                 "DELETE FROM RepoGroup WHERE "
                                 "user_name = ? AND group_id = ? "
                                 "AND repo_id IN "
                                 "(SELECT repo_id FROM VirtualRepo WHERE "
                                 "origin_repo = ? AND path = ?)",
                                 4, "string", owner,
                                 "int", group_id,
                                 "string", repo_id,
                                 "string", path) < 0)
        return -1;

    return 0;
}

gboolean
syncw_share_manager_repo_has_been_shared (SyncwShareManager* mgr,
                                         const char *repo_id,
                                         gboolean including_groups)
{
    gboolean exists;
    gboolean db_err = FALSE;
    char *sql;

    sql = "SELECT 1 FROM SharedRepo WHERE repo_id=?";
    exists = syncwerk_server_db_statement_exists (mgr->syncw->db, sql, &db_err,
                                       1, "string", repo_id);
    if (db_err) {
        syncw_warning ("DB error when check repo exist in SharedRepo and RepoGroup.\n");
        return FALSE;
    }

    if (!exists && including_groups) {
        sql = "SELECT 1 FROM RepoGroup WHERE repo_id=?";
        exists = syncwerk_server_db_statement_exists (mgr->syncw->db, sql, &db_err,
                                           1, "string", repo_id);
    }

    return exists;
}

gboolean
get_shared_users_cb (SyncwDBRow *row, void *data)
{
    GList **users = data;
    const char *repo_id = syncwerk_server_db_row_get_column_text (row, 0);
    const char *user = syncwerk_server_db_row_get_column_text (row, 1);
    const char *perm = syncwerk_server_db_row_get_column_text (row, 2);
    SyncwerkSharedUser *uobj = g_object_new (SYNCWERK_TYPE_SHARED_USER,
                                            "repo_id", repo_id,
                                            "user", user,
                                            "perm", perm,
                                            NULL);
    *users = g_list_append (*users, uobj);

    return TRUE;
}

GList *
syncw_share_manager_org_get_shared_users_by_repo (SyncwShareManager* mgr,
                                                 int org_id,
                                                 const char *repo_id)
{
    GList *users = NULL;
    char *sql = "SELECT repo_id, to_email, permission FROM OrgSharedRepo WHERE org_id=? AND "
                "repo_id=?";

    int ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                             get_shared_users_cb, &users,
                                             2, "int", org_id, "string", repo_id);
    if (ret < 0) {
        syncw_warning("Failed to get users by repo_id[%s], org_id[%d]\n",
                     repo_id, org_id);
        return NULL;
    }

    return users;
}


GList *
syncw_share_manager_get_shared_users_by_repo(SyncwShareManager* mgr,
                                            const char *repo_id)
{
    GList *users = NULL;
    char *sql = "SELECT repo_id, to_email, permission FROM SharedRepo WHERE "
                "repo_id=?";

    int ret = syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql,
                                             get_shared_users_cb, &users,
                                             1, "string", repo_id);
    if (ret < 0) {
        syncw_warning("Failed to get users by repo_id[%s]\n", repo_id);
        return NULL;
    }

    return users;
}
