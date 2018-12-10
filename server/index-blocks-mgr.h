#ifndef INDEX_BLOCKS_MGR_H
#define INDEX_BLOCKS_MGR_H

#include "syncwerk-object.h"

struct IndexBlksMgrPriv;
struct _SyncwerkSession;

typedef struct IndexBlksMgr {
    struct IndexBlksMgrPriv *priv;
} IndexBlksMgr;

typedef struct IdxProgress {
    gint64 indexed;
    gint64 total;
    int status; /* 0: finished, -1: error, 1: indexing */
    char *ret_json;
    gint64 expire_ts;
} IdxProgress;

IndexBlksMgr *
index_blocks_mgr_new (struct _SyncwerkSession *session);

char *
index_blocks_mgr_query_progress (IndexBlksMgr *mgr,
                                 const char *token,
                                 GError **error);

int
index_blocks_mgr_start_index (IndexBlksMgr *mgr,
                              GList *filenames,
                              GList *paths,
                              const char *repo_id,
                              const char *user,
                              int replace_existed,
                              gboolean ret_json,
                              const char *canon_path,
                              SyncwerkCrypt *crypt,
                              char **task_id);

#endif
