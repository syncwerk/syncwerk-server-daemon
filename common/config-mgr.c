#include "common.h"
#include "config-mgr.h"
#include "syncwerk-server-db.h"
#include "log.h"

int
syncw_cfg_manager_init (SyncwCfgManager *mgr)
{
    char *sql;
    int db_type = syncwerk_server_db_type(mgr->db);

    if (db_type == SYNCW_DB_TYPE_MYSQL)
        sql = "CREATE TABLE IF NOT EXISTS SyncwerkConf ("
              "id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT, cfg_group VARCHAR(255) NOT NULL,"
              "cfg_key VARCHAR(255) NOT NULL, value VARCHAR(255), property INTEGER) ENGINE=INNODB";
    else
        sql = "CREATE TABLE IF NOT EXISTS SyncwerkConf (cfg_group VARCHAR(255) NOT NULL,"
              "cfg_key VARCHAR(255) NOT NULL, value VARCHAR(255), property INTEGER)";

    if (syncwerk_server_db_query (mgr->db, sql) < 0)
        return -1;

    return 0;
}

SyncwCfgManager *
syncw_cfg_manager_new (SyncwerkSession *session)
{
    SyncwCfgManager *mgr = g_new0 (SyncwCfgManager, 1);
    if (!mgr)
        return NULL;

    mgr->config = session->config;
    mgr->db = session->db;

    return mgr;
}

int
syncw_cfg_manager_set_config_int (SyncwCfgManager *mgr,
                                 const char *group,
                                 const char *key,
                                 int value)
{
    char value_str[256];

    snprintf (value_str, sizeof(value_str), "%d", value);

    return syncw_cfg_manager_set_config (mgr, group, key, value_str);
}

int
syncw_cfg_manager_set_config_int64 (SyncwCfgManager *mgr,
                                   const char *group,
                                   const char *key,
                                   gint64 value)
{
    char value_str[256];

    snprintf (value_str, sizeof(value_str), "%"G_GINT64_FORMAT"", value);

    return syncw_cfg_manager_set_config (mgr, group, key, value_str);
}

int
syncw_cfg_manager_set_config_string (SyncwCfgManager *mgr,
                                    const char *group,
                                    const char *key,
                                    const char *value)
{
    char value_str[256];

    snprintf (value_str, sizeof(value_str), "%s", value);

    return syncw_cfg_manager_set_config (mgr, group, key, value_str);
}

int
syncw_cfg_manager_set_config_boolean (SyncwCfgManager *mgr,
                                     const char *group,
                                     const char *key,
                                     gboolean value)
{
    char value_str[256];

    if (value)
        snprintf (value_str, sizeof(value_str), "true");
    else
        snprintf (value_str, sizeof(value_str), "false");

    return syncw_cfg_manager_set_config (mgr, group, key, value_str);
}

int
syncw_cfg_manager_set_config (SyncwCfgManager *mgr, const char *group, const char *key, const char *value)
{
    gboolean exists, err = FALSE;

    char *sql = "SELECT 1 FROM SyncwerkConf WHERE cfg_group=? AND cfg_key=?";
    exists = syncwerk_server_db_statement_exists(mgr->db, sql, &err,
                                      2, "string", group,
                                      "string", key);
    if (err) {
        syncw_warning ("[db error]Failed to set config [%s:%s] to db.\n", group, key);
        return -1;
    }
    if (exists)
        sql = "UPDATE SyncwerkConf SET value=? WHERE cfg_group=? AND cfg_key=?";
    else
        sql = "INSERT INTO SyncwerkConf (value, cfg_group, cfg_key, property) VALUES "
              "(?,?,?,0)";
    if (syncwerk_server_db_statement_query (mgr->db, sql, 3,
                                 "string", value, "string",
                                 group, "string", key) < 0) {
        syncw_warning ("Failed to set config [%s:%s] to db.\n", group, key);
        return -1;
    }

    return 0;
}

int
syncw_cfg_manager_get_config_int (SyncwCfgManager *mgr, const char *group, const char *key)
{
    int ret;
    char *invalid = NULL;

    char *value = syncw_cfg_manager_get_config (mgr, group, key);
    if (!value) {
        GError *err = NULL;
        ret = g_key_file_get_integer (mgr->config, group, key, &err);
        if (err) {
            ret = -1;
            g_clear_error(&err);
        }
    } else {
        ret = strtol (value, &invalid, 10);
        if (*invalid != '\0') {
            ret = -1;
            syncw_warning ("Value of config [%s:%s] is invalid: [%s]\n", group, key, value);
        }
        g_free (value);
    }

    return ret;
}

gint64
syncw_cfg_manager_get_config_int64 (SyncwCfgManager *mgr, const char *group, const char *key)
{
    gint64 ret;
    char *invalid = NULL;

    char *value = syncw_cfg_manager_get_config (mgr, group, key);
    if (!value) {
        GError *err = NULL;
        ret = g_key_file_get_int64(mgr->config, group, key, &err);
        if (err) {
            ret = -1;
            g_clear_error(&err);
        }
    } else {
        ret = strtoll (value, &invalid, 10);
        if (*invalid != '\0') {
            syncw_warning ("Value of config [%s:%s] is invalid: [%s]\n", group, key, value);
            ret = -1;
        }
        g_free (value);
    }

    return ret;
}

gboolean
syncw_cfg_manager_get_config_boolean (SyncwCfgManager *mgr, const char *group, const char *key)
{
    gboolean ret;

    char *value = syncw_cfg_manager_get_config (mgr, group, key);
    if (!value) {
        GError *err = NULL;
        ret = g_key_file_get_boolean(mgr->config, group, key, &err);
        if (err) {
            syncw_warning ("Config [%s:%s] not set, default is false.\n", group, key);
            ret = FALSE;
            g_clear_error(&err);
        }
    } else {
        if (strcmp ("true", value) == 0)
            ret = TRUE;
        else
            ret = FALSE;
        g_free (value);
    }

    return ret;
}

char *
syncw_cfg_manager_get_config_string (SyncwCfgManager *mgr, const char *group, const char *key)
{
    char *ret = NULL;

    char *value = syncw_cfg_manager_get_config (mgr, group, key);
    if (!value) {
        ret = g_key_file_get_string (mgr->config, group, key, NULL);
        if (ret != NULL)
            ret = g_strstrip(ret);
    } else {
        ret = value;
    }

    return ret;
}

char *
syncw_cfg_manager_get_config (SyncwCfgManager *mgr, const char *group, const char *key)
{
    char *sql = "SELECT value FROM SyncwerkConf WHERE cfg_group=? AND cfg_key=?";
    char *value = syncwerk_server_db_statement_get_string(mgr->db, sql, 
                                               2, "string", group, "string", key);
    if (value != NULL)
        value = g_strstrip(value);

    return value;
}
