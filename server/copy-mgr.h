#ifndef COPY_MGR_H
#define COPY_MGR_H

#include <glib.h>

struct _SyncwerkSession;
struct _SyncwCopyManagerPriv;
struct _SyncwerkCopyTask;

struct _SyncwCopyManager {
    struct _SyncwerkSession *session;
    struct _SyncwCopyManagerPriv *priv;

    gint64 max_files;
    gint64 max_size;
};
typedef struct _SyncwCopyManager SyncwCopyManager;
typedef struct _SyncwCopyManagerPriv SyncwCopyManagerPriv;

struct CopyTask {
    char task_id[37];
    gint64 done;
    gint64 total;
    gint canceled;
    gboolean failed;
    gboolean successful;
};
typedef struct CopyTask CopyTask;

SyncwCopyManager *
syncw_copy_manager_new (struct _SyncwerkSession *session);

int
syncw_copy_manager_start (SyncwCopyManager *mgr);

typedef int (*CopyTaskFunc) (const char *, const char *, const char *,
                             const char *, const char *, const char *,
                             int, const char *, CopyTask *);

char *
syncw_copy_manager_add_task (SyncwCopyManager *mgr,
                            const char *src_repo_id,
                            const char *src_path,
                            const char *src_filename,
                            const char *dst_repo_id,
                            const char *dst_path,
                            const char *dst_filename,
                            int replace,
                            const char *modifier,
                            gint64 total_files,
                            CopyTaskFunc function,
                            gboolean need_progress);

struct _SyncwerkCopyTask *
syncw_copy_manager_get_task (SyncwCopyManager *mgr,
                            const char * id);

int
syncw_copy_manager_cancel_task (SyncwCopyManager *mgr, const char *task_id);

#endif
