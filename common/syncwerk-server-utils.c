#include "common.h"

#include "log.h"

#include "syncwerk-session.h"
#include "syncwerk-server-utils.h"
#include "syncwerk-server-db.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

char *
syncwerk_session_get_tmp_file_path (SyncwerkSession *session,
                                   const char *basename,
                                   char path[])
{
    int path_len;

    path_len = strlen (session->tmp_file_dir);
    memcpy (path, session->tmp_file_dir, path_len + 1);
    path[path_len] = '/';
    strcpy (path + path_len + 1, basename);

    return path;
}

#define SQLITE_DB_NAME "syncwerk.db"

#define DEFAULT_MAX_CONNECTIONS 100

static int
sqlite_db_start (SyncwerkSession *session)
{
    char *db_path;
    int max_connections = 0;

    max_connections = g_key_file_get_integer (session->config,
                                              "database", "max_connections",
                                              NULL);
    if (max_connections <= 0)
        max_connections = DEFAULT_MAX_CONNECTIONS;

    db_path = g_build_filename (session->syncw_dir, SQLITE_DB_NAME, NULL);
    session->db = syncwerk_server_db_new_sqlite (db_path, max_connections);
    if (!session->db) {
        syncw_warning ("Failed to start sqlite db.\n");
        return -1;
    }

    return 0;
}

#ifdef HAVE_MYSQL

#define MYSQL_DEFAULT_PORT 3306

static int
mysql_db_start (SyncwerkSession *session)
{
    char *host, *user, *passwd, *db, *unix_socket, *charset;
    int port;
    gboolean use_ssl = FALSE;
    gboolean create_tables = TRUE;
    int max_connections = 0;
    GError *error = NULL;

    host = syncw_key_file_get_string (session->config, "database", "host", &error);
    if (!host) {
        syncw_warning ("DB host not set in config.\n");
        return -1;
    }

    port = g_key_file_get_integer (session->config, "database", "port", &error);
    if (error) {
        port = MYSQL_DEFAULT_PORT;
    }

    user = syncw_key_file_get_string (session->config, "database", "user", &error);
    if (!user) {
        syncw_warning ("DB user not set in config.\n");
        return -1;
    }

    passwd = syncw_key_file_get_string (session->config, "database", "password", &error);
    if (!passwd) {
        syncw_warning ("DB passwd not set in config.\n");
        return -1;
    }

    db = syncw_key_file_get_string (session->config, "database", "db_name", &error);
    if (!db) {
        syncw_warning ("DB name not set in config.\n");
        return -1;
    }

    unix_socket = syncw_key_file_get_string (session->config, 
                                         "database", "unix_socket", NULL);

    use_ssl = g_key_file_get_boolean (session->config,
                                      "database", "use_ssl", NULL);

    if (g_key_file_has_key (session->config, "database", "create_tables", NULL)) {
        create_tables = g_key_file_get_boolean (session->config,
                                                "database", "create_tables", NULL);
    }
    session->create_tables = create_tables;

    charset = syncw_key_file_get_string (session->config,
                                     "database", "connection_charset", NULL);

    max_connections = g_key_file_get_integer (session->config,
                                              "database", "max_connections",
                                              NULL);
    if (max_connections <= 0)
        max_connections = DEFAULT_MAX_CONNECTIONS;

    session->db = syncwerk_server_db_new_mysql (host, port, user, passwd, db, unix_socket, use_ssl, charset, max_connections);
    if (!session->db) {
        syncw_warning ("Failed to start mysql db.\n");
        return -1;
    }

    g_free (host);
    g_free (user);
    g_free (passwd);
    g_free (db);
    g_free (unix_socket);
    g_free (charset);
    if (error)
        g_clear_error (&error);

    return 0;
}

#endif

#ifdef HAVE_POSTGRESQL

static int
pgsql_db_start (SyncwerkSession *session)
{
    char *host, *user, *passwd, *db, *unix_socket;
    unsigned int port;
    GError *error = NULL;

    host = syncw_key_file_get_string (session->config, "database", "host", &error);
    if (!host) {
        syncw_warning ("DB host not set in config.\n");
        return -1;
    }

    user = syncw_key_file_get_string (session->config, "database", "user", &error);
    if (!user) {
        syncw_warning ("DB user not set in config.\n");
        return -1;
    }

    passwd = syncw_key_file_get_string (session->config, "database", "password", &error);
    if (!passwd) {
        syncw_warning ("DB passwd not set in config.\n");
        return -1;
    }

    db = syncw_key_file_get_string (session->config, "database", "db_name", &error);
    if (!db) {
        syncw_warning ("DB name not set in config.\n");
        return -1;
    }
    port = g_key_file_get_integer (session->config,
                                   "database", "port", &error);
    if (error) {
        port = 0;
        g_clear_error (&error);
    }

    unix_socket = syncw_key_file_get_string (session->config,
                                         "database", "unix_socket", &error);

    session->db = syncwerk_server_db_new_pgsql (host, port, user, passwd, db, unix_socket,
                                     DEFAULT_MAX_CONNECTIONS);
    if (!session->db) {
        syncw_warning ("Failed to start pgsql db.\n");
        return -1;
    }

    g_free (host);
    g_free (user);
    g_free (passwd);
    g_free (db);
    g_free (unix_socket);

    return 0;
}

#endif

int
load_database_config (SyncwerkSession *session)
{
    char *type;
    GError *error = NULL;
    int ret = 0;

    type = syncw_key_file_get_string (session->config, "database", "type", &error);
    /* Default to use sqlite if not set. */
    if (!type || strcasecmp (type, "sqlite") == 0) {
        ret = sqlite_db_start (session);
    }
#ifdef HAVE_MYSQL
    else if (strcasecmp (type, "mysql") == 0) {
        ret = mysql_db_start (session);
    }
#endif
#ifdef HAVE_POSTGRESQL
    else if (strcasecmp (type, "pgsql") == 0) {
        ret = pgsql_db_start (session);
    }
#endif
    else {
        syncw_warning ("Unsupported db type %s.\n", type);
        ret = -1;
    }

    g_free (type);

    return ret;
}
