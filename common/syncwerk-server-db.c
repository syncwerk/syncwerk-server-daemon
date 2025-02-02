
#include "common.h"

#include "log.h"

#include "db-wrapper/db-wrapper.h"
#include "syncwerk-server-db.h"

#define MAX_GET_CONNECTION_RETRIES 3

struct SyncwDB {
    int type;
    DBConnPool *pool;
};

struct SyncwDBRow {
    ResultSet *res;
};

struct SyncwDBTrans {
    DBConnection *conn;
};

#ifdef HAVE_MYSQL

SyncwDB *
syncwerk_server_db_new_mysql (const char *host,
                   int port,
                   const char *user, 
                   const char *passwd,
                   const char *db_name,
                   const char *unix_socket,
                   gboolean use_ssl,
                   const char *charset,
                   int max_connections)
{
    SyncwDB *db;

    db = g_new0 (SyncwDB, 1);
    if (!db) {
        syncw_warning ("Failed to alloc db structure.\n");
        return NULL;
    }

    db->type = SYNCW_DB_TYPE_MYSQL;

    db->pool = db_conn_pool_new_mysql (host, user, passwd, port, db_name,
                                       unix_socket, use_ssl, charset, max_connections);

    return db;
}

#endif

#ifdef HAVE_POSTGRESQL

SyncwDB *
syncwerk_server_db_new_pgsql (const char *host,
                   unsigned int port,
                   const char *user,
                   const char *passwd,
                   const char *db_name,
                   const char *unix_socket,
                   int max_connections)
{
    SyncwDB *db;

    db = g_new0 (SyncwDB, 1);
    if (!db) {
        syncw_warning ("Failed to alloc db structure.\n");
        return NULL;
    }

    db->type = SYNCW_DB_TYPE_PGSQL;

    db->pool = db_conn_pool_new_pgsql (host, port, user, passwd, db_name, unix_socket,
                                       max_connections);

    return db;
}

#endif

SyncwDB *
syncwerk_server_db_new_sqlite (const char *db_path, int max_connections)
{
    SyncwDB *db;

    db = g_new0 (SyncwDB, 1);
    if (!db) {
        syncw_warning ("Failed to alloc db structure.\n");
        return NULL;
    }

    db->type = SYNCW_DB_TYPE_SQLITE;

    db->pool = db_conn_pool_new_sqlite (db_path, max_connections);

    return db;
}

void
syncwerk_server_db_free (SyncwDB *db)
{
    db_conn_pool_free (db->pool);
    g_free (db);
}

int
syncwerk_server_db_type (SyncwDB *db)
{
    return db->type;
}

static DBConnection *
get_db_connection (SyncwDB *db)
{
    DBConnection *conn;
    GError *error = NULL;

    conn = db_conn_pool_get_connection (db->pool, &error);

    if (!conn) {
        syncw_warning ("Failed to get database connection: %s.\n", error->message);
        g_clear_error (&error);
    }

    return conn;
}

int
syncwerk_server_db_query (SyncwDB *db, const char *sql)
{
    GError *error = NULL;
    int ret = 0;

    DBConnection *conn = get_db_connection (db);
    if (!conn)
        return -1;

    if (!db_connection_execute (conn, sql, &error)) {
        syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        ret = -1;
    }

    db_connection_close (conn);

    return ret;
}

gboolean
syncwerk_server_db_check_for_existence (SyncwDB *db, const char *sql, gboolean *db_err)
{
    DBConnection *conn;
    ResultSet *result;
    gboolean ret = TRUE;
    GError *error = NULL;

    *db_err = FALSE;

    conn = get_db_connection (db);
    if (!conn) {
        *db_err = TRUE;
        return FALSE;
    }

    result = db_connection_execute_query (conn, sql, &error);
    if (error) {
        syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        *db_err = TRUE;
        ret = FALSE;
        goto out;
    }

    ret = result_set_next (result, &error);
    if (error) {
        syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        *db_err = TRUE;
    }

out:
    db_connection_close (conn);

    return ret;
}

int
syncwerk_server_db_foreach_selected_row (SyncwDB *db, const char *sql, 
                              SyncwDBRowFunc callback, void *data)
{
    DBConnection *conn;
    ResultSet *result;
    SyncwDBRow syncw_row;
    int n_rows = 0;
    GError *error = NULL;

    conn = get_db_connection (db);
    if (!conn)
        return -1;

    result = db_connection_execute_query (conn, sql, &error);
    if (error) {
        syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        n_rows = -1;
        goto out;
    }

    syncw_row.res = result;

    while (result_set_next (result, &error)) {
        n_rows++;
        if (!callback (&syncw_row, data))
            break;
    }

    if (error) {
        syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        n_rows = -1;
    }

out:
    db_connection_close (conn);
    return n_rows;
}

const char *
syncwerk_server_db_row_get_column_text (SyncwDBRow *row, guint32 idx)
{
    GError *error = NULL;
    g_return_val_if_fail (idx < result_set_get_column_count(row->res), NULL);

    return result_set_get_string (row->res, idx, &error);
}

int
syncwerk_server_db_row_get_column_int (SyncwDBRow *row, guint32 idx)
{
    GError *error = NULL;
    g_return_val_if_fail (idx < result_set_get_column_count(row->res), -1);

    return result_set_get_int (row->res, idx, &error);
}

gint64
syncwerk_server_db_row_get_column_int64 (SyncwDBRow *row, guint32 idx)
{
    GError *error = NULL;
    g_return_val_if_fail (idx < result_set_get_column_count(row->res), -1);

    return result_set_get_int64 (row->res, idx, &error);
}

int
syncwerk_server_db_get_int (SyncwDB *db, const char *sql)
{
    int ret = -1;
    DBConnection *conn;
    ResultSet *result;
    SyncwDBRow syncw_row;
    GError *error = NULL;

    conn = get_db_connection (db);
    if (!conn)
        return -1;

    result = db_connection_execute_query (conn, sql, &error);
    if (error) {
        syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        goto out;
    }

    syncw_row.res = result;
    if (!result_set_next (result, &error)) {
        if (error) {
            syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
            g_clear_error (&error);
        }
        goto out;
    }

    ret = syncwerk_server_db_row_get_column_int (&syncw_row, 0);

out:
    db_connection_close (conn);
    return ret;
}

gint64
syncwerk_server_db_get_int64 (SyncwDB *db, const char *sql)
{
    gint64 ret = -1;
    DBConnection *conn;
    ResultSet *result;
    SyncwDBRow syncw_row;
    GError *error = NULL;

    conn = get_db_connection (db);
    if (!conn)
        return -1;

    result = db_connection_execute_query (conn, sql, &error);
    if (error) {
        syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        goto out;
    }

    syncw_row.res = result;
    if (!result_set_next (result, &error)) {
        if (error) {
            syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
            g_clear_error (&error);
        }
        goto out;
    }

    ret = syncwerk_server_db_row_get_column_int64 (&syncw_row, 0);

out:
    db_connection_close (conn);
    return ret;
}

char *
syncwerk_server_db_get_string (SyncwDB *db, const char *sql)
{
    char *ret = NULL;
    const char *s;
    DBConnection *conn;
    ResultSet *result;
    SyncwDBRow syncw_row;
    GError *error = NULL;

    conn = get_db_connection (db);
    if (!conn)
        return NULL;

    result = db_connection_execute_query (conn, sql, &error);
    if (error) {
        syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        goto out;
    }

    syncw_row.res = result;
    if (!result_set_next (result, &error)) {
        if (error) {
            syncw_warning ("Error exec query %s: %s.\n", sql, error->message);
            g_clear_error (&error);
        }
        goto out;
    }

    s = syncwerk_server_db_row_get_column_text (&syncw_row, 0);
    ret = g_strdup(s);

out:
    db_connection_close (conn);
    return ret;
}

char *
syncwerk_server_db_escape_string (SyncwDB *db, const char *from)
{
    const char *p = from;
    char *to, *q;

    to = g_malloc0 (2*strlen(from)+1);
    q = to;

    while (*p != '\0') {
        if (*p == '\'' || *p == '\\' || *p == '"') {
            *q = *p;
            *(++q) = *p;
        } else
            *q = *p;
        ++p;
        ++q;
    }

    return to;
}

gboolean
pgsql_index_exists (SyncwDB *db, const char *index_name)
{
    char sql[256];
    gboolean db_err = FALSE;

    snprintf (sql, sizeof(sql),
              "SELECT 1 FROM pg_class WHERE relname='%s'",
              index_name);
    return syncwerk_server_db_check_for_existence (db, sql, &db_err);
}

/* Prepared Statements */

struct SyncwDBStatement {
    DBStmt *p;
    DBConnection *conn;
};
typedef struct SyncwDBStatement SyncwDBStatement;

SyncwDBStatement *
syncwerk_server_db_prepare_statement (SyncwDB *db, const char *sql)
{
    DBStmt *p;
    SyncwDBStatement *ret = g_new0 (SyncwDBStatement, 1);
    GError *error = NULL;

    DBConnection *conn = get_db_connection (db);
    if (!conn) {
        g_free (ret);
        return NULL;
    }

    p = db_connection_prepare_statement (conn, sql, &error);
    if (!p) {
        syncw_warning ("Error prepare statement %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        g_free (ret);
        db_connection_close (conn);
        return NULL;
    }

    ret->p = p;
    ret->conn = conn;

    return ret;
}

void
syncwerk_server_db_statement_free (SyncwDBStatement *p)
{
    db_connection_close (p->conn);
    g_free (p);
}

int
syncwerk_server_db_statement_set_int (DBStmt *p, int idx, int x)
{
    GError *error = NULL;

    if (!db_stmt_set_int (p, idx, x, &error)) {
        syncw_warning ("Error set int in prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        return -1;
    }

    return 0;
}

int
syncwerk_server_db_statement_set_string (DBStmt *p, int idx, const char *s)
{
    GError *error = NULL;

    if (!db_stmt_set_string (p, idx, s, &error)) {
        syncw_warning ("Error set string in prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        return -1;
    }

    return 0;
}

int
syncwerk_server_db_statement_set_int64 (DBStmt *p, int idx, gint64 x)
{
    GError *error = NULL;

    if (!db_stmt_set_int64 (p, idx, x, &error)) {
        syncw_warning ("Error set int64 in prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        return -1;
    }

    return 0;
}

static int
set_parameters_va (DBStmt *p, int n, va_list args)
{
    int i;
    const char *type;

    for (i = 0; i < n; ++i) {
        type = va_arg (args, const char *);
        if (strcmp(type, "int") == 0) {
            int x = va_arg (args, int);
            if (syncwerk_server_db_statement_set_int (p, i, x) < 0)
                return -1;
        } else if (strcmp (type, "int64") == 0) {
            gint64 x = va_arg (args, gint64);
            if (syncwerk_server_db_statement_set_int64 (p, i, x) < 0)
                return -1;
        } else if (strcmp (type, "string") == 0) {
            const char *s = va_arg (args, const char *);
            if (syncwerk_server_db_statement_set_string (p, i, s) < 0)
                return -1;
        } else {
            syncw_warning ("BUG: invalid prep stmt parameter type %s.\n", type);
            g_return_val_if_reached (-1);
        }
    }

    return 0;
}

int
syncwerk_server_db_statement_query (SyncwDB *db, const char *sql, int n, ...)
{
    SyncwDBStatement *p;
    int ret = 0;
    GError *error = NULL;

    p = syncwerk_server_db_prepare_statement (db, sql);
    if (!p)
        return -1;

    va_list args;
    va_start (args, n);
    if (set_parameters_va (p->p, n, args) < 0) {
        syncwerk_server_db_statement_free (p);
        va_end (args);
        return -1;
    }
    va_end (args);

    if (!db_stmt_execute (p->p, &error)) {
        syncw_warning ("Error execute prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        ret = -1;
    }

    syncwerk_server_db_statement_free (p);
    return ret;
}

gboolean
syncwerk_server_db_statement_exists (SyncwDB *db, const char *sql, gboolean *db_err, int n, ...)
{
    SyncwDBStatement *p;
    ResultSet *result;
    gboolean ret = TRUE;
    GError *error = NULL;

    *db_err = FALSE;

    p = syncwerk_server_db_prepare_statement (db, sql);
    if (!p) {
        *db_err = TRUE;
        return FALSE;
    }

    va_list args;
    va_start (args, n);
    if (set_parameters_va (p->p, n, args) < 0) {
        syncwerk_server_db_statement_free (p);
        *db_err = TRUE;
        va_end (args);
        return FALSE;
    }
    va_end (args);

    result = db_stmt_execute_query (p->p, &error);
    if (error) {
        syncw_warning ("Error exec prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        *db_err = TRUE;
        ret = FALSE;
        goto out;
    }

    if (!result_set_next (result, &error))
        ret = FALSE;

    if (error) {
        syncw_warning ("Error get next result from prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        *db_err = TRUE;
    }

out:
    syncwerk_server_db_statement_free (p);
    return ret;
}

int
syncwerk_server_db_statement_foreach_row (SyncwDB *db,
                                const char *sql,
                                SyncwDBRowFunc callback, void *data,
                                int n, ...)
{
    SyncwDBStatement *p;
    ResultSet *result;
    SyncwDBRow syncw_row;
    int n_rows = 0;
    GError *error = NULL;

    p = syncwerk_server_db_prepare_statement (db, sql);
    if (!p)
        return -1;

    va_list args;
    va_start (args, n);
    if (set_parameters_va (p->p, n, args) < 0) {
        syncwerk_server_db_statement_free (p);
        va_end (args);
        return -1;
    }
    va_end (args);

    result = db_stmt_execute_query (p->p, &error);
    if (error) {
        syncw_warning ("Error exec prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        n_rows = -1;
        goto out;
    }

    syncw_row.res = result;

    while (result_set_next (result, &error)) {
        n_rows++;
        if (!callback (&syncw_row, data))
            break;
    }

    if (error) {
        syncw_warning ("Error get next result from prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        n_rows = -1;
    }

out:
    syncwerk_server_db_statement_free (p);
    return n_rows;
}

int
syncwerk_server_db_statement_get_int (SyncwDB *db, const char *sql, int n, ...)
{
    SyncwDBStatement *p;
    int ret = -1;
    ResultSet *result;
    SyncwDBRow syncw_row;
    GError *error = NULL;

    p = syncwerk_server_db_prepare_statement (db, sql);
    if (!p)
        return -1;

    va_list args;
    va_start (args, n);
    if (set_parameters_va (p->p, n, args) < 0) {
        syncwerk_server_db_statement_free (p);
        va_end (args);
        return -1;
    }
    va_end (args);

    result = db_stmt_execute_query (p->p, &error);
    if (error) {
        syncw_warning ("Error exec prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        goto out;
    }

    syncw_row.res = result;
    if (!result_set_next (result, &error)) {
        if (error) {
            syncw_warning ("Error get next result from prep stmt: %s.\n", error->message);
            g_clear_error (&error);
        }
        goto out;
    }

    ret = syncwerk_server_db_row_get_column_int (&syncw_row, 0);

out:
    syncwerk_server_db_statement_free (p);
    return ret;
}

gint64
syncwerk_server_db_statement_get_int64 (SyncwDB *db, const char *sql, int n, ...)
{
    SyncwDBStatement *p;
    gint64 ret = -1;
    ResultSet *result;
    SyncwDBRow syncw_row;
    GError *error = NULL;

    p = syncwerk_server_db_prepare_statement (db, sql);
    if (!p)
        return -1;

    va_list args;
    va_start (args, n);
    if (set_parameters_va (p->p, n, args) < 0) {
        syncwerk_server_db_statement_free (p);
        va_end (args);
        return -1;
    }
    va_end (args);

    result = db_stmt_execute_query (p->p, &error);
    if (error) {
        syncw_warning ("Error exec prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        goto out;
    }

    syncw_row.res = result;
    if (!result_set_next (result, &error)) {
        if (error) {
            syncw_warning ("Error get next result from prep stmt: %s.\n", error->message);
            g_clear_error (&error);
        }
        goto out;
    }

    ret = syncwerk_server_db_row_get_column_int64 (&syncw_row, 0);

out:
    syncwerk_server_db_statement_free (p);
    return ret;
}

char *
syncwerk_server_db_statement_get_string (SyncwDB *db, const char *sql, int n, ...)
{
    SyncwDBStatement *p;
    char *ret = NULL;
    const char *s;
    ResultSet *result;
    SyncwDBRow syncw_row;
    GError *error = NULL;

    p = syncwerk_server_db_prepare_statement (db, sql);
    if (!p)
        return NULL;

    va_list args;
    va_start (args, n);
    if (set_parameters_va (p->p, n, args) < 0) {
        syncwerk_server_db_statement_free (p);
        va_end (args);
        return NULL;
    }
    va_end (args);

    result = db_stmt_execute_query (p->p, &error);
    if (error) {
        syncw_warning ("Error exec prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        goto out;
    }

    syncw_row.res = result;
    if (!result_set_next (result, &error)) {
        if (error) {
            syncw_warning ("Error get next result from prep stmt: %s.\n", error->message);
            g_clear_error (&error);
        }
        goto out;
    }

    s = syncwerk_server_db_row_get_column_text (&syncw_row, 0);
    ret = g_strdup(s);

out:
    syncwerk_server_db_statement_free (p);
    return ret;
}

/* Transaction */

SyncwDBTrans *
syncwerk_server_db_begin_transaction (SyncwDB *db)
{
    DBConnection *conn;
    SyncwDBTrans *trans;
    GError *error = NULL;

    trans = g_new0 (SyncwDBTrans, 1);
    if (!trans)
        return NULL;

    conn = get_db_connection (db);
    if (!conn) {
        g_free (trans);
        return NULL;
    }

    trans->conn = conn;
    if (!db_connection_begin_transaction (trans->conn, &error)) {
        syncw_warning ("Start transaction failed: %s.\n", error->message);
        g_clear_error (&error);
        db_connection_close (trans->conn);
        g_free (trans);
        return NULL;
    }

    return trans;
}

void
syncwerk_server_db_trans_close (SyncwDBTrans *trans)
{
    db_connection_close (trans->conn);
    g_free (trans);
}

int
syncwerk_server_db_commit (SyncwDBTrans *trans)
{
    DBConnection *conn = trans->conn;
    GError *error = NULL;

    if (!db_connection_commit (conn, &error)) {
        syncw_warning ("Commit failed: %s.\n", error->message);
        g_clear_error (&error);
        return -1;
    }

    return 0;
}

int
syncwerk_server_db_rollback (SyncwDBTrans *trans)
{
    DBConnection *conn = trans->conn;
    GError *error = NULL;

    if (!db_connection_rollback (conn, &error)) {
        syncw_warning ("Rollback failed: %s.\n", error->message);
        g_clear_error (&error);
        return -1;
    }

    return 0;
}

static DBStmt *
trans_prepare_statement (DBConnection *conn, const char *sql)
{
    DBStmt *p;
    GError *error = NULL;

    p = db_connection_prepare_statement (conn, sql, &error);
    if (!p) {
        syncw_warning ("Error prepare statement %s: %s.\n", sql, error->message);
        g_clear_error (&error);
        return NULL;
    }

    return p;
}

int
syncwerk_server_db_trans_query (SyncwDBTrans *trans, const char *sql, int n, ...)
{
    DBStmt *p;
    GError *error = NULL;

    p = trans_prepare_statement (trans->conn, sql);
    if (!p)
        return -1;

    va_list args;
    va_start (args, n);
    if (set_parameters_va (p, n, args) < 0) {
        va_end (args);
        return -1;
    }
    va_end (args);

    if (!db_stmt_execute (p, &error)) {
        syncw_warning ("Error exec prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        return -1;
    }

    return 0;
}

gboolean
syncwerk_server_db_trans_check_for_existence (SyncwDBTrans *trans,
                                   const char *sql,
                                   gboolean *db_err,
                                   int n, ...)
{
    ResultSet *result;
    gboolean ret = TRUE;
    GError *error = NULL;

    *db_err = FALSE;

    DBStmt *p;

    p = trans_prepare_statement (trans->conn, sql);
    if (!p) {
        *db_err = TRUE;
        return FALSE;
    }

    va_list args;
    va_start (args, n);
    if (set_parameters_va (p, n, args) < 0) {
        *db_err = TRUE;
        va_end (args);
        return FALSE;
    }
    va_end (args);

    result = db_stmt_execute_query (p, &error);
    if (error) {
        syncw_warning ("Error exec prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        *db_err = TRUE;
        return FALSE;
    }

    if (!result_set_next (result, &error))
        ret = FALSE;

    if (error) {
        syncw_warning ("Error get next result from prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        *db_err = TRUE;
    }

    return ret;
}

int
syncwerk_server_db_trans_foreach_selected_row (SyncwDBTrans *trans, const char *sql, 
                                    SyncwDBRowFunc callback, void *data,
                                    int n, ...)
{
    ResultSet *result;
    SyncwDBRow syncw_row;
    int n_rows = 0;
    GError *error = NULL;

    DBStmt *p;

    p = trans_prepare_statement (trans->conn, sql);
    if (!p)
        return FALSE;

    va_list args;
    va_start (args, n);
    if (set_parameters_va (p, n, args) < 0) {
        va_end (args);
        return -1;
    }
    va_end (args);

    result = db_stmt_execute_query (p, &error);
    if (error) {
        syncw_warning ("Error exec prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        return -1;
    }

    syncw_row.res = result;

    while (result_set_next (result, &error)) {
        n_rows++;
        if (!callback (&syncw_row, data))
            break;
    }

    if (error) {
        syncw_warning ("Error get next result from prep stmt: %s.\n", error->message);
        g_clear_error (&error);
        n_rows = -1;
    }

    return n_rows;
}
