/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <ccnet.h>

#include "db.h"
#include "syncwerk-session.h"
#include "permission-mgr.h"

#define PERM_DB "perm.db"

struct _SyncwPermManagerPriv {
    sqlite3    *db;
};

static int load_db (SyncwPermManager *mgr);

SyncwPermManager *
syncw_perm_manager_new (SyncwerkSession *syncw)
{
    SyncwPermManager *mgr = g_new0 (SyncwPermManager, 1);
    mgr->priv = g_new0 (SyncwPermManagerPriv, 1);
    mgr->syncw = syncw;
    return mgr;
}

int
syncw_perm_manager_init (SyncwPermManager *mgr)
{
    return load_db (mgr);
}

static int
load_db (SyncwPermManager *mgr)
{
    char *db_path = g_build_filename (mgr->syncw->syncw_dir, PERM_DB, NULL);
    if (sqlite_open_db (db_path, &mgr->priv->db) < 0) {
        g_critical ("[Permission mgr] Failed to open permission db\n");
        g_free (db_path);
        g_free (mgr);
        return -1;
    }
    g_free (db_path);

    const char *sql;

    return 0;
}

