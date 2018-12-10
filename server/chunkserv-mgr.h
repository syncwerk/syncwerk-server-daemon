#ifndef CHUNKSERV_MGR_H
#define CHUNKSERV_MGR_H

#include <glib.h>
#include <db.h>

struct _SyncwerkSession;

struct _SyncwCSManager {
    struct _SyncwerkSession      *syncw;
    GHashTable          *chunk_servers;
    sqlite3             *db;
};
typedef struct _SyncwCSManager SyncwCSManager;

SyncwCSManager*  syncw_cs_manager_new (struct _SyncwerkSession *syncw);
int             syncw_cs_manager_start (SyncwCSManager *mgr);

int             syncw_cs_manager_add_chunk_server (SyncwCSManager *mgr, const char *cs_id);
int             syncw_cs_manager_del_chunk_server (SyncwCSManager *mgr, const char *cs_id);
GList*          syncw_cs_manager_get_chunk_servers (SyncwCSManager *mgr);

#endif
