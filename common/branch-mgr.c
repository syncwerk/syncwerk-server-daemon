#include "common.h"

#include "log.h"

#ifndef SYNCWERK_SERVER
#include "db.h"
#else
#include "syncwerk-server-db.h"
#endif

#include "syncwerk-session.h"

#include "branch-mgr.h"

#define BRANCH_DB "branch.db"

SyncwBranch *
syncw_branch_new (const char *name, const char *repo_id, const char *commit_id)
{
    SyncwBranch *branch;

    branch = g_new0 (SyncwBranch, 1);

    branch->name = g_strdup (name);
    memcpy (branch->repo_id, repo_id, 36);
    branch->repo_id[36] = '\0';
    memcpy (branch->commit_id, commit_id, 40);
    branch->commit_id[40] = '\0';

    branch->ref = 1;

    return branch;
}

void
syncw_branch_free (SyncwBranch *branch)
{
    if (branch == NULL) return;
    g_free (branch->name);
    g_free (branch);
}

void
syncw_branch_list_free (GList *blist)
{
    GList *ptr;

    for (ptr = blist; ptr; ptr = ptr->next) {
        syncw_branch_unref (ptr->data);
    }
    g_list_free (blist);
}


void
syncw_branch_set_commit (SyncwBranch *branch, const char *commit_id)
{
    memcpy (branch->commit_id, commit_id, 40);
    branch->commit_id[40] = '\0';
}

void
syncw_branch_ref (SyncwBranch *branch)
{
    branch->ref++;
}

void
syncw_branch_unref (SyncwBranch *branch)
{
    if (!branch)
        return;

    if (--branch->ref <= 0)
        syncw_branch_free (branch);
}

struct _SyncwBranchManagerPriv {
    sqlite3 *db;
#ifndef SYNCWERK_SERVER
    pthread_mutex_t db_lock;
#endif

#if defined( SYNCWERK_SERVER ) && defined( FULL_FEATURE )
    uint32_t cevent_id;
#endif    
};

#if defined( SYNCWERK_SERVER ) && defined( FULL_FEATURE )

#include "mq-mgr.h"
#include <ccnet/cevent.h>
static void publish_repo_update_event (CEvent *event, void *data);

#endif    

static int open_db (SyncwBranchManager *mgr);

SyncwBranchManager *
syncw_branch_manager_new (struct _SyncwerkSession *syncw)
{
    SyncwBranchManager *mgr;

    mgr = g_new0 (SyncwBranchManager, 1);
    mgr->priv = g_new0 (SyncwBranchManagerPriv, 1);
    mgr->syncw = syncw;

#ifndef SYNCWERK_SERVER
    pthread_mutex_init (&mgr->priv->db_lock, NULL);
#endif

    return mgr;
}

int
syncw_branch_manager_init (SyncwBranchManager *mgr)
{
#if defined( SYNCWERK_SERVER ) && defined( FULL_FEATURE )
    mgr->priv->cevent_id = cevent_manager_register (syncw->ev_mgr,
                                    (cevent_handler)publish_repo_update_event,
                                                    NULL);
#endif    

    return open_db (mgr);
}

static int
open_db (SyncwBranchManager *mgr)
{
    if (!mgr->syncw->create_tables && syncwerk_server_db_type (mgr->syncw->db) == SYNCW_DB_TYPE_MYSQL)
        return 0;
#ifndef SYNCWERK_SERVER

    char *db_path;
    const char *sql;

    db_path = g_build_filename (mgr->syncw->syncw_dir, BRANCH_DB, NULL);
    if (sqlite_open_db (db_path, &mgr->priv->db) < 0) {
        g_critical ("[Branch mgr] Failed to open branch db\n");
        g_free (db_path);
        return -1;
    }
    g_free (db_path);

    sql = "CREATE TABLE IF NOT EXISTS Branch ("
          "name TEXT, repo_id TEXT, commit_id TEXT);";
    if (sqlite_query_exec (mgr->priv->db, sql) < 0)
        return -1;

    sql = "CREATE INDEX IF NOT EXISTS branch_index ON Branch(repo_id, name);";
    if (sqlite_query_exec (mgr->priv->db, sql) < 0)
        return -1;

#elif defined FULL_FEATURE

    char *sql;
    switch (syncwerk_server_db_type (mgr->syncw->db)) {
    case SYNCW_DB_TYPE_MYSQL:
        sql = "CREATE TABLE IF NOT EXISTS Branch ("
            "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, "
            "name VARCHAR(10), repo_id CHAR(41), commit_id CHAR(41),"
            "UNIQUE INDEX(repo_id, name)) ENGINE = INNODB";
        if (syncwerk_server_db_query (mgr->syncw->db, sql) < 0)
            return -1;
        break;
    case SYNCW_DB_TYPE_PGSQL:
        sql = "CREATE TABLE IF NOT EXISTS Branch ("
            "name VARCHAR(10), repo_id CHAR(40), commit_id CHAR(40),"
            "PRIMARY KEY (repo_id, name))";
        if (syncwerk_server_db_query (mgr->syncw->db, sql) < 0)
            return -1;
        break;
    case SYNCW_DB_TYPE_SQLITE:
        sql = "CREATE TABLE IF NOT EXISTS Branch ("
            "name VARCHAR(10), repo_id CHAR(41), commit_id CHAR(41),"
            "PRIMARY KEY (repo_id, name))";
        if (syncwerk_server_db_query (mgr->syncw->db, sql) < 0)
            return -1;
        break;
    }

#endif

    return 0;
}

int
syncw_branch_manager_add_branch (SyncwBranchManager *mgr, SyncwBranch *branch)
{
#ifndef SYNCWERK_SERVER
    char sql[256];

    pthread_mutex_lock (&mgr->priv->db_lock);

    sqlite3_snprintf (sizeof(sql), sql,
                      "SELECT 1 FROM Branch WHERE name=%Q and repo_id=%Q",
                      branch->name, branch->repo_id);
    if (sqlite_check_for_existence (mgr->priv->db, sql))
        sqlite3_snprintf (sizeof(sql), sql,
                          "UPDATE Branch SET commit_id=%Q WHERE "
                          "name=%Q and repo_id=%Q",
                          branch->commit_id, branch->name, branch->repo_id);
    else
        sqlite3_snprintf (sizeof(sql), sql,
                          "INSERT INTO Branch (name, repo_id, commit_id) VALUES (%Q, %Q, %Q)",
                          branch->name, branch->repo_id, branch->commit_id);

    sqlite_query_exec (mgr->priv->db, sql);

    pthread_mutex_unlock (&mgr->priv->db_lock);

    return 0;
#else
    char *sql;
    SyncwDB *db = mgr->syncw->db;

    if (syncwerk_server_db_type(db) == SYNCW_DB_TYPE_PGSQL) {
        gboolean exists, err;
        int rc;

        sql = "SELECT repo_id FROM Branch WHERE name=? AND repo_id=?";
        exists = syncwerk_server_db_statement_exists(db, sql, &err,
                                          2, "string", branch->name,
                                          "string", branch->repo_id);
        if (err)
            return -1;

        if (exists)
            rc = syncwerk_server_db_statement_query (db,
                                          "UPDATE Branch SET commit_id=? "
                                          "WHERE name=? AND repo_id=?",
                                          3, "string", branch->commit_id,
                                          "string", branch->name,
                                          "string", branch->repo_id);
        else
            rc = syncwerk_server_db_statement_query (db,
                                          "INSERT INTO Branch (name, repo_id, commit_id) VALUES (?, ?, ?)",
                                          3, "string", branch->name,
                                          "string", branch->repo_id,
                                          "string", branch->commit_id);
        if (rc < 0)
            return -1;
    } else {
        int rc = syncwerk_server_db_statement_query (db,
                                 "REPLACE INTO Branch (name, repo_id, commit_id) VALUES (?, ?, ?)",
                                 3, "string", branch->name,
                                 "string", branch->repo_id,
                                 "string", branch->commit_id);
        if (rc < 0)
            return -1;
    }
    return 0;
#endif
}

int
syncw_branch_manager_del_branch (SyncwBranchManager *mgr,
                                const char *repo_id,
                                const char *name)
{
#ifndef SYNCWERK_SERVER
    char *sql;

    pthread_mutex_lock (&mgr->priv->db_lock);

    sql = sqlite3_mprintf ("DELETE FROM Branch WHERE name = %Q AND "
                           "repo_id = '%s'", name, repo_id);
    if (sqlite_query_exec (mgr->priv->db, sql) < 0)
        syncw_warning ("Delete branch %s failed\n", name);
    sqlite3_free (sql);

    pthread_mutex_unlock (&mgr->priv->db_lock);

    return 0;
#else
    int rc = syncwerk_server_db_statement_query (mgr->syncw->db,
                                      "DELETE FROM Branch WHERE name=? AND repo_id=?",
                                      2, "string", name, "string", repo_id);
    if (rc < 0)
        return -1;
    return 0;
#endif
}

int
syncw_branch_manager_update_branch (SyncwBranchManager *mgr, SyncwBranch *branch)
{
#ifndef SYNCWERK_SERVER
    sqlite3 *db;
    char *sql;

    pthread_mutex_lock (&mgr->priv->db_lock);

    db = mgr->priv->db;
    sql = sqlite3_mprintf ("UPDATE Branch SET commit_id = %Q "
                           "WHERE name = %Q AND repo_id = %Q",
                           branch->commit_id, branch->name, branch->repo_id);
    sqlite_query_exec (db, sql);
    sqlite3_free (sql);

    pthread_mutex_unlock (&mgr->priv->db_lock);

    return 0;
#else
    int rc = syncwerk_server_db_statement_query (mgr->syncw->db,
                                      "UPDATE Branch SET commit_id = ? "
                                      "WHERE name = ? AND repo_id = ?",
                                      3, "string", branch->commit_id,
                                      "string", branch->name,
                                      "string", branch->repo_id);
    if (rc < 0)
        return -1;
    return 0;
#endif
}

#if defined( SYNCWERK_SERVER ) && defined( FULL_FEATURE )

static gboolean
get_commit_id (SyncwDBRow *row, void *data)
{
    char *out_commit_id = data;
    const char *commit_id;

    commit_id = syncwerk_server_db_row_get_column_text (row, 0);
    memcpy (out_commit_id, commit_id, 41);
    out_commit_id[40] = '\0';

    return FALSE;
}

typedef struct {
    char *repo_id;
    char *commit_id;
} RepoUpdateEventData;

static void
publish_repo_update_event (CEvent *event, void *data)
{
    RepoUpdateEventData *rdata = event->data;

    char buf[128];
    snprintf (buf, sizeof(buf), "repo-update\t%s\t%s",
              rdata->repo_id, rdata->commit_id);

    syncw_mq_manager_publish_event (syncw->mq_mgr, buf);

    g_free (rdata->repo_id);
    g_free (rdata->commit_id);
    g_free (rdata);
}

static void
on_branch_updated (SyncwBranchManager *mgr, SyncwBranch *branch)
{
    if (syncw_repo_manager_is_virtual_repo (syncw->repo_mgr, branch->repo_id))
        return;

    RepoUpdateEventData *rdata = g_new0 (RepoUpdateEventData, 1);

    rdata->repo_id = g_strdup (branch->repo_id);
    rdata->commit_id = g_strdup (branch->commit_id);
    
    cevent_manager_add_event (syncw->ev_mgr, mgr->priv->cevent_id, rdata);

    syncw_repo_manager_update_repo_info (syncw->repo_mgr, branch->repo_id, branch->commit_id);
}

int
syncw_branch_manager_test_and_update_branch (SyncwBranchManager *mgr,
                                            SyncwBranch *branch,
                                            const char *old_commit_id)
{
    SyncwDBTrans *trans;
    char *sql;
    char commit_id[41] = { 0 };

    trans = syncwerk_server_db_begin_transaction (mgr->syncw->db);
    if (!trans)
        return -1;

    switch (syncwerk_server_db_type (mgr->syncw->db)) {
    case SYNCW_DB_TYPE_MYSQL:
    case SYNCW_DB_TYPE_PGSQL:
        sql = "SELECT commit_id FROM Branch WHERE name=? "
            "AND repo_id=? FOR UPDATE";
        break;
    case SYNCW_DB_TYPE_SQLITE:
        sql = "SELECT commit_id FROM Branch WHERE name=? "
            "AND repo_id=?";
        break;
    default:
        g_return_val_if_reached (-1);
    }
    if (syncwerk_server_db_trans_foreach_selected_row (trans, sql,
                                            get_commit_id, commit_id,
                                            2, "string", branch->name,
                                            "string", branch->repo_id) < 0) {
        syncwerk_server_db_rollback (trans);
        syncwerk_server_db_trans_close (trans);
        return -1;
    }
    if (strcmp (old_commit_id, commit_id) != 0) {
        syncwerk_server_db_rollback (trans);
        syncwerk_server_db_trans_close (trans);
        return -1;
    }

    sql = "UPDATE Branch SET commit_id = ? "
        "WHERE name = ? AND repo_id = ?";
    if (syncwerk_server_db_trans_query (trans, sql, 3, "string", branch->commit_id,
                             "string", branch->name,
                             "string", branch->repo_id) < 0) {
        syncwerk_server_db_rollback (trans);
        syncwerk_server_db_trans_close (trans);
        return -1;
    }

    if (syncwerk_server_db_commit (trans) < 0) {
        syncwerk_server_db_rollback (trans);
        syncwerk_server_db_trans_close (trans);
        return -1;
    }

    syncwerk_server_db_trans_close (trans);

    on_branch_updated (mgr, branch);

    return 0;
}

#endif

#ifndef SYNCWERK_SERVER
static SyncwBranch *
real_get_branch (SyncwBranchManager *mgr,
                 const char *repo_id,
                 const char *name)
{
    SyncwBranch *branch = NULL;
    sqlite3_stmt *stmt;
    sqlite3 *db;
    char *sql;
    int result;

    pthread_mutex_lock (&mgr->priv->db_lock);

    db = mgr->priv->db;
    sql = sqlite3_mprintf ("SELECT commit_id FROM Branch "
                           "WHERE name = %Q and repo_id='%s'",
                           name, repo_id);
    if (!(stmt = sqlite_query_prepare (db, sql))) {
        syncw_warning ("[Branch mgr] Couldn't prepare query %s\n", sql);
        sqlite3_free (sql);
        pthread_mutex_unlock (&mgr->priv->db_lock);
        return NULL;
    }
    sqlite3_free (sql);

    result = sqlite3_step (stmt);
    if (result == SQLITE_ROW) {
        char *commit_id = (char *)sqlite3_column_text (stmt, 0);

        branch = syncw_branch_new (name, repo_id, commit_id);
        pthread_mutex_unlock (&mgr->priv->db_lock);
        sqlite3_finalize (stmt);
        return branch;
    } else if (result == SQLITE_ERROR) {
        const char *str = sqlite3_errmsg (db);
        syncw_warning ("Couldn't prepare query, error: %d->'%s'\n",
                   result, str ? str : "no error given");
    }

    sqlite3_finalize (stmt);
    pthread_mutex_unlock (&mgr->priv->db_lock);
    return NULL;
}

SyncwBranch *
syncw_branch_manager_get_branch (SyncwBranchManager *mgr,
                                const char *repo_id,
                                const char *name)
{
    SyncwBranch *branch;

    /* "fetch_head" maps to "local" or "master" on client (LAN sync) */
    if (strcmp (name, "fetch_head") == 0) {
        branch = real_get_branch (mgr, repo_id, "local");
        if (!branch) {
            branch = real_get_branch (mgr, repo_id, "master");
        }
        return branch;
    } else {
        return real_get_branch (mgr, repo_id, name);
    }
}

#else

static gboolean
get_branch (SyncwDBRow *row, void *vid)
{
    char *ret = vid;
    const char *commit_id;

    commit_id = syncwerk_server_db_row_get_column_text (row, 0);
    memcpy (ret, commit_id, 41);

    return FALSE;
}

static SyncwBranch *
real_get_branch (SyncwBranchManager *mgr,
                 const char *repo_id,
                 const char *name)
{
    char commit_id[41];
    char *sql;

    commit_id[0] = 0;
    sql = "SELECT commit_id FROM Branch WHERE name=? AND repo_id=?";
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, 
                                       get_branch, commit_id,
                                       2, "string", name, "string", repo_id) < 0) {
        syncw_warning ("[branch mgr] DB error when get branch %s.\n", name);
        return NULL;
    }

    if (commit_id[0] == 0)
        return NULL;

    return syncw_branch_new (name, repo_id, commit_id);
}

SyncwBranch *
syncw_branch_manager_get_branch (SyncwBranchManager *mgr,
                                const char *repo_id,
                                const char *name)
{
    SyncwBranch *branch;

    /* "fetch_head" maps to "master" on server. */
    if (strcmp (name, "fetch_head") == 0) {
        branch = real_get_branch (mgr, repo_id, "master");
        return branch;
    } else {
        return real_get_branch (mgr, repo_id, name);
    }
}

#endif  /* not SYNCWERK_SERVER */

gboolean
syncw_branch_manager_branch_exists (SyncwBranchManager *mgr,
                                   const char *repo_id,
                                   const char *name)
{
#ifndef SYNCWERK_SERVER
    char *sql;
    gboolean ret;

    pthread_mutex_lock (&mgr->priv->db_lock);

    sql = sqlite3_mprintf ("SELECT name FROM Branch WHERE name = %Q "
                           "AND repo_id='%s'", name, repo_id);
    ret = sqlite_check_for_existence (mgr->priv->db, sql);
    sqlite3_free (sql);

    pthread_mutex_unlock (&mgr->priv->db_lock);
    return ret;
#else
    gboolean db_err = FALSE;

    return syncwerk_server_db_statement_exists (mgr->syncw->db,
                                     "SELECT name FROM Branch WHERE name=? "
                                     "AND repo_id=?", &db_err,
                                     2, "string", name, "string", repo_id);
#endif
}

#ifndef SYNCWERK_SERVER
GList *
syncw_branch_manager_get_branch_list (SyncwBranchManager *mgr,
                                     const char *repo_id)
{
    sqlite3 *db = mgr->priv->db;
    
    int result;
    sqlite3_stmt *stmt;
    char sql[256];
    char *name;
    char *commit_id;
    GList *ret = NULL;
    SyncwBranch *branch;

    snprintf (sql, 256, "SELECT name, commit_id FROM branch WHERE repo_id ='%s'",
              repo_id);

    pthread_mutex_lock (&mgr->priv->db_lock);

    if ( !(stmt = sqlite_query_prepare(db, sql)) ) {
        pthread_mutex_unlock (&mgr->priv->db_lock);
        return NULL;
    }

    while (1) {
        result = sqlite3_step (stmt);
        if (result == SQLITE_ROW) {
            name = (char *)sqlite3_column_text(stmt, 0);
            commit_id = (char *)sqlite3_column_text(stmt, 1);
            branch = syncw_branch_new (name, repo_id, commit_id);
            ret = g_list_prepend (ret, branch);
        }
        if (result == SQLITE_DONE)
            break;
        if (result == SQLITE_ERROR) {
            const gchar *str = sqlite3_errmsg (db);
            syncw_warning ("Couldn't prepare query, error: %d->'%s'\n", 
                       result, str ? str : "no error given");
            sqlite3_finalize (stmt);
            syncw_branch_list_free (ret);
            pthread_mutex_unlock (&mgr->priv->db_lock);
            return NULL;
        }
    }

    sqlite3_finalize (stmt);
    pthread_mutex_unlock (&mgr->priv->db_lock);
    return g_list_reverse(ret);
}
#else
static gboolean
get_branches (SyncwDBRow *row, void *vplist)
{
    GList **plist = vplist;
    const char *commit_id;
    const char *name;
    const char *repo_id;
    SyncwBranch *branch;

    name = syncwerk_server_db_row_get_column_text (row, 0);
    repo_id = syncwerk_server_db_row_get_column_text (row, 1);
    commit_id = syncwerk_server_db_row_get_column_text (row, 2);

    branch = syncw_branch_new (name, repo_id, commit_id);
    *plist = g_list_prepend (*plist, branch);

    return TRUE;
}

GList *
syncw_branch_manager_get_branch_list (SyncwBranchManager *mgr,
                                     const char *repo_id)
{
    GList *ret = NULL;
    char *sql;

    sql = "SELECT name, repo_id, commit_id FROM Branch WHERE repo_id=?";
    if (syncwerk_server_db_statement_foreach_row (mgr->syncw->db, sql, 
                                       get_branches, &ret,
                                       1, "string", repo_id) < 0) {
        syncw_warning ("[branch mgr] DB error when get branch list.\n");
        return NULL;
    }

    return ret;
}
#endif
