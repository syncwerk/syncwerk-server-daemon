/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_SESSION_H
#define SYNCWERK_SESSION_H

#include <ccnet.h>
#include <ccnet/cevent.h>
#include <ccnet/job-mgr.h>

#include "block-mgr.h"
#include "fs-mgr.h"
#include "commit-mgr.h"
#include "branch-mgr.h"
#include "repo-mgr.h"
#include "db.h"
#include "syncwerk-server-db.h"

#include "chunkserv-mgr.h"
#include "share-mgr.h"
#include "token-mgr.h"
#include "web-accesstoken-mgr.h"
#include "passwd-mgr.h"
#include "quota-mgr.h"
#include "listen-mgr.h"
#include "size-sched.h"
#include "copy-mgr.h"
#include "config-mgr.h"

#include "mq-mgr.h"

#include "http-server.h"
#include "zip-download-mgr.h"
#include "index-blocks-mgr.h"

#include <rpcsyncwerk-client.h>

struct _CcnetClient;

typedef struct _SyncwerkSession SyncwerkSession;


struct _SyncwerkSession {
    struct _CcnetClient *session;

    RpcsyncwerkClient        *ccnetrpc_client;
    RpcsyncwerkClient        *ccnetrpc_client_t;
    /* Use async rpc client on server. */
    RpcsyncwerkClient        *async_ccnetrpc_client;
    RpcsyncwerkClient        *async_ccnetrpc_client_t;

    /* Used in threads. */
    CcnetClientPool     *client_pool;

    char                *central_config_dir;
    char                *syncw_dir;
    char                *tmp_file_dir;
    /* Config that's only loaded on start */
    GKeyFile            *config;
    SyncwDB              *db;

    SyncwBlockManager    *block_mgr;
    SyncwFSManager       *fs_mgr;
    SyncwCommitManager   *commit_mgr;
    SyncwBranchManager   *branch_mgr;
    SyncwRepoManager     *repo_mgr;
    SyncwCSManager       *cs_mgr;
    SyncwShareManager	*share_mgr;
    SyncwTokenManager    *token_mgr;
    SyncwPasswdManager   *passwd_mgr;
    SyncwQuotaManager    *quota_mgr;
    SyncwListenManager   *listen_mgr;
    SyncwCopyManager     *copy_mgr;
    SyncwCfgManager      *cfg_mgr;
    
    SyncwWebAccessTokenManager	*web_at_mgr;

    SyncwMqManager       *mq_mgr;

    CEventManager       *ev_mgr;
    CcnetJobManager     *job_mgr;

    SizeScheduler       *size_sched;

    int                  is_master;

    int                  cloud_mode;

    int                  rpc_thread_pool_size;
    int                  sync_thread_pool_size;

    HttpServerStruct    *http_server;
    ZipDownloadMgr      *zip_download_mgr;
    IndexBlksMgr        *index_blocks_mgr;

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

char *
syncwerk_session_get_tmp_file_path (SyncwerkSession *session,
                                   const char *basename,
                                   char path[]);

void
schedule_create_system_default_repo (SyncwerkSession *session);

char *
get_system_default_repo_id (SyncwerkSession *session);

int
set_system_default_repo_id (SyncwerkSession *session, const char *repo_id);

#endif /* SYNCWERK_H */
