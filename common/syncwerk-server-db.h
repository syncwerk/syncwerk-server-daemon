#ifndef SYNCW_DB_H
#define SYNCW_DB_H

enum {
    SYNCW_DB_TYPE_SQLITE,
    SYNCW_DB_TYPE_MYSQL,
    SYNCW_DB_TYPE_PGSQL,
};

typedef struct SyncwDB SyncwDB;
typedef struct SyncwDBRow SyncwDBRow;
typedef struct SyncwDBTrans SyncwDBTrans;

typedef gboolean (*SyncwDBRowFunc) (SyncwDBRow *, void *);

SyncwDB *
syncwerk_server_db_new_mysql (const char *host,
                   int port,
                   const char *user, 
                   const char *passwd,
                   const char *db,
                   const char *unix_socket,
                   gboolean use_ssl,
                   const char *charset,
                   int max_connections);

SyncwDB *
syncwerk_server_db_new_pgsql (const char *host,
                   unsigned int port,
                   const char *user,
                   const char *passwd,
                   const char *db_name,
                   const char *unix_socket,
                   int max_connections);

SyncwDB *
syncwerk_server_db_new_sqlite (const char *db_path, int max_connections);

void
syncwerk_server_db_free (SyncwDB *db);

int
syncwerk_server_db_type (SyncwDB *db);

int
syncwerk_server_db_query (SyncwDB *db, const char *sql);

gboolean
syncwerk_server_db_check_for_existence (SyncwDB *db, const char *sql, gboolean *db_err);

int
syncwerk_server_db_foreach_selected_row (SyncwDB *db, const char *sql, 
                              SyncwDBRowFunc callback, void *data);

const char *
syncwerk_server_db_row_get_column_text (SyncwDBRow *row, guint32 idx);

int
syncwerk_server_db_row_get_column_int (SyncwDBRow *row, guint32 idx);

gint64
syncwerk_server_db_row_get_column_int64 (SyncwDBRow *row, guint32 idx);

int
syncwerk_server_db_get_int (SyncwDB *db, const char *sql);

gint64
syncwerk_server_db_get_int64 (SyncwDB *db, const char *sql);

char *
syncwerk_server_db_get_string (SyncwDB *db, const char *sql);

/* Transaction related */

SyncwDBTrans *
syncwerk_server_db_begin_transaction (SyncwDB *db);

void
syncwerk_server_db_trans_close (SyncwDBTrans *trans);

int
syncwerk_server_db_commit (SyncwDBTrans *trans);

int
syncwerk_server_db_rollback (SyncwDBTrans *trans);

int
syncwerk_server_db_trans_query (SyncwDBTrans *trans, const char *sql, int n, ...);

gboolean
syncwerk_server_db_trans_check_for_existence (SyncwDBTrans *trans,
                                   const char *sql,
                                   gboolean *db_err,
                                   int n, ...);

int
syncwerk_server_db_trans_foreach_selected_row (SyncwDBTrans *trans, const char *sql,
                                    SyncwDBRowFunc callback, void *data,
                                    int n, ...);

/* Escape a string contant by doubling '\" characters.
 */
char *
syncwerk_server_db_escape_string (SyncwDB *db, const char *from);

gboolean
pgsql_index_exists (SyncwDB *db, const char *index_name);

/* Prepared Statements */

int
syncwerk_server_db_statement_query (SyncwDB *db, const char *sql, int n, ...);

gboolean
syncwerk_server_db_statement_exists (SyncwDB *db, const char *sql, gboolean *db_err, int n, ...);

int
syncwerk_server_db_statement_foreach_row (SyncwDB *db, const char *sql,
                                SyncwDBRowFunc callback, void *data,
                                int n, ...);

int
syncwerk_server_db_statement_get_int (SyncwDB *db, const char *sql, int n, ...);

gint64
syncwerk_server_db_statement_get_int64 (SyncwDB *db, const char *sql, int n, ...);

char *
syncwerk_server_db_statement_get_string (SyncwDB *db, const char *sql, int n, ...);

#endif
