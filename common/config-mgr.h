#ifndef SYNCW_CONFIG_MGR_H
#define SYNCW_CONFIG_MGR_H

typedef struct _SyncwCfgManager SyncwCfgManager;
#include "syncwerk-session.h"

struct _SyncwCfgManager {
    GKeyFile *config;
    SyncwDB *db;
};

typedef struct _SyncwerkSession SyncwerkSession;

SyncwCfgManager *
syncw_cfg_manager_new (SyncwerkSession *syncw);

int
syncw_cfg_manager_set_config (SyncwCfgManager *mgr, const char *group, const char *key, const char *value);

char *
syncw_cfg_manager_get_config (SyncwCfgManager *mgr, const char *group, const char *key);

int
syncw_cfg_manager_set_config_int (SyncwCfgManager *mgr, const char *group, const char *key, int value);

int
syncw_cfg_manager_get_config_int (SyncwCfgManager *mgr, const char *group, const char *key);

int
syncw_cfg_manager_set_config_int64 (SyncwCfgManager *mgr, const char *group, const char *key, gint64 value);

gint64
syncw_cfg_manager_get_config_int64 (SyncwCfgManager *mgr, const char *group, const char *key);

int
syncw_cfg_manager_set_config_string (SyncwCfgManager *mgr, const char *group, const char *key, const char *value);

char *
syncw_cfg_manager_get_config_string (SyncwCfgManager *mgr, const char *group, const char *key);

int
syncw_cfg_manager_set_config_boolean (SyncwCfgManager *mgr, const char *group, const char *key, gboolean value);

gboolean
syncw_cfg_manager_get_config_boolean (SyncwCfgManager *mgr, const char *group, const char *key);

int
syncw_cfg_manager_init (SyncwCfgManager *mgr);

#endif /* SYNCW_CONFIG_MGR_H */
