#ifndef SYNCWERK_SESSION_H
#define SYNCWERK_SESSION_H

#include <stdint.h>
#include <glib.h>

#include <syncwerk-server-db.h>

#include "block-mgr.h"
#include "fs-mgr.h"
#include "branch-mgr.h"
#include "commit-mgr.h"
#include "repo-mgr.h"

struct _CcnetClient;

typedef struct _SyncwerkSession SyncwerkSession;

struct CcnetClientPool;

struct _SyncwerkSession {
    struct _CcnetClient *session;

    char                *syncw_dir;
    char                *tmp_file_dir;
    /* Config that's only loaded on start */
    GKeyFile            *config;
    SyncwDB              *db;

    struct CcnetClientPool     *client_pool;

    SyncwBlockManager    *block_mgr;
    SyncwFSManager       *fs_mgr;
    SyncwBranchManager   *branch_mgr;
    SyncwCommitManager   *commit_mgr;
    SyncwRepoManager     *repo_mgr;

    gboolean create_tables;
};

extern SyncwerkSession *syncw;

SyncwerkSession *
syncwerk_session_new(const char *central_config_dir,
                    const char *syncwerk_dir,
                    struct _CcnetClient *ccnet_session);

int
syncwerk_session_init (SyncwerkSession *session);

int
syncwerk_session_start (SyncwerkSession *session);

#endif
