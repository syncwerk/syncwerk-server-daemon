/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#define DEBUG_FLAG SYNCWERK_DEBUG_TRANSFER
#include "log.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <ccnet.h>
#include "utils.h"

#include "syncwerk-session.h"
#include "fs-mgr.h"
#include "processors/objecttx-common.h"
#include "recvfs-proc.h"
#include "syncwerk-server-utils.h"

#define CHECK_INTERVAL 100      /* 100ms */
#define MAX_NUM_BATCH  64
#define MAX_CHECKING_DIRS 1000

enum {
    RECV_ROOT,
    FETCH_OBJECT
};

typedef struct  {
    GList *fs_roots;
    int n_roots;

    int inspect_objects;
    int pending_objects;
    char buf[4096];
    char *bufptr;
    int  n_batch;

    GHashTable  *fs_objects;

    int checking_dirs;
    GQueue *dir_queue;

    char *obj_seg;
    int obj_seg_len;

    gboolean registered;
    guint32  reader_id;
    guint32  writer_id;
    guint32  stat_id;

    /* Used for getting repo info */
    char        repo_id[37];
    char        store_id[37];
    int         repo_version;
    gboolean    success;
} SyncwerkRecvfsProcPriv;

#define GET_PRIV(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), SYNCWERK_TYPE_RECVFS_PROC, SyncwerkRecvfsProcPriv))

#define USE_PRIV \
    SyncwerkRecvfsProcPriv *priv = GET_PRIV(processor);


G_DEFINE_TYPE (SyncwerkRecvfsProc, syncwerk_recvfs_proc, CCNET_TYPE_PROCESSOR)

static int start (CcnetProcessor *processor, int argc, char **argv);
static void handle_update (CcnetProcessor *processor,
                           char *code, char *code_msg,
                           char *content, int clen);

static void
free_dir_id (gpointer data, gpointer user_data)
{
    g_free (data);
}

static void
release_resource(CcnetProcessor *processor)
{
    USE_PRIV;

    if (priv->fs_objects)
        g_hash_table_destroy (priv->fs_objects);

    string_list_free (priv->fs_roots);

    g_queue_foreach (priv->dir_queue, free_dir_id, NULL);
    g_queue_free (priv->dir_queue);

    g_free (priv->obj_seg);
    
    if (priv->registered) {
        syncw_obj_store_unregister_async_read (syncw->fs_mgr->obj_store,
                                              priv->reader_id);
        syncw_obj_store_unregister_async_write (syncw->fs_mgr->obj_store,
                                               priv->writer_id);
        syncw_obj_store_unregister_async_stat (syncw->fs_mgr->obj_store,
                                              priv->stat_id);
    }

    CCNET_PROCESSOR_CLASS (syncwerk_recvfs_proc_parent_class)->release_resource (processor);
}

static void
syncwerk_recvfs_proc_class_init (SyncwerkRecvfsProcClass *klass)
{
    CcnetProcessorClass *proc_class = CCNET_PROCESSOR_CLASS (klass);

    proc_class->name = "recvfs-proc";
    proc_class->start = start;
    proc_class->handle_update = handle_update;
    proc_class->release_resource = release_resource;

    g_type_class_add_private (klass, sizeof (SyncwerkRecvfsProcPriv));
}

static void
syncwerk_recvfs_proc_init (SyncwerkRecvfsProc *processor)
{
}

inline static void
request_object_batch_begin (SyncwerkRecvfsProcPriv *priv)
{
    priv->bufptr = priv->buf;
    priv->n_batch = 0;
}

inline static void
request_object_batch_flush (CcnetProcessor *processor,
                            SyncwerkRecvfsProcPriv *priv)
{
    if (priv->bufptr == priv->buf)
        return;
    *priv->bufptr = '\0';       /* add ending '\0' */
    priv->bufptr++;
    ccnet_processor_send_response (processor, SC_GET_OBJECT, SS_GET_OBJECT,
                                   priv->buf, priv->bufptr - priv->buf);

    /* Clean state */
    priv->n_batch = 0;
    priv->bufptr = priv->buf;
}

inline static void
request_object_batch (CcnetProcessor *processor,
                      SyncwerkRecvfsProcPriv *priv, const char *id)
{
    memcpy (priv->bufptr, id, 40);
    priv->bufptr += 40;
    *priv->bufptr = '\n';
    priv->bufptr++;

    /* Flush when too many objects batched. */
    if (++priv->n_batch == MAX_NUM_BATCH)
        request_object_batch_flush (processor, priv);
    ++priv->pending_objects;
}

static int
check_syncwdir (CcnetProcessor *processor, SyncwDir *dir)
{
    USE_PRIV;
    GList *ptr;
    SyncwDirent *dent;

    for (ptr = dir->entries; ptr != NULL; ptr = ptr->next) {
        dent = ptr->data;

        if (strcmp (dent->id, EMPTY_SHA1) == 0)
            continue;

        /* Don't check objects that have been checked before. */
        if (priv->fs_objects && g_hash_table_lookup (priv->fs_objects, dent->id))
            continue;

        if (S_ISDIR(dent->mode)) {
            g_queue_push_tail (priv->dir_queue, g_strdup(dent->id));
        } else {
#ifdef DEBUG
            syncw_debug ("[recvfs] Inspect file %s.\n", dent->id);
#endif

            /* For file, we just need to check existence. */
            if (syncw_obj_store_async_stat (syncw->fs_mgr->obj_store,
                                           priv->stat_id,
                                           dent->id) < 0) {
                syncw_warning ("[recvfs] Failed to start async stat of %s.\n",
                           dent->id);
                goto bad;
            }
            ++(priv->inspect_objects);
        }

        if (priv->fs_objects)
            g_hash_table_insert (priv->fs_objects, g_strdup(dent->id), (gpointer)1);
    }

    return 0;

bad:
    ccnet_processor_send_response (processor, SC_BAD_OBJECT, SS_BAD_OBJECT,
                                   NULL, 0);
    ccnet_processor_done (processor, FALSE);
    return -1;
}

static void
on_syncwdir_read (OSAsyncResult *res, void *cb_data)
{
    CcnetProcessor *processor = cb_data;
    SyncwDir *dir;
    USE_PRIV;

    --(priv->inspect_objects);
    --(priv->checking_dirs);

    if (!res->success) {
        request_object_batch (processor, priv, res->obj_id);
        return;
    }

#ifdef DEBUG
    syncw_debug ("[recvfs] Read syncwdir %s.\n", res->obj_id);
#endif

    dir = syncw_dir_from_data (res->obj_id, res->data, res->len,
                              (priv->repo_version > 0));
    if (!dir) {
        syncw_warning ("[recvfs] Corrupt dir object %s.\n", res->obj_id);
        request_object_batch (processor, priv, res->obj_id);
        return;
    }

    int ret = check_syncwdir (processor, dir);
    syncw_dir_free (dir);
    if (ret < 0)
        return;
}

static void
on_syncwerk_stat (OSAsyncResult *res, void *cb_data)
{
    CcnetProcessor *processor = cb_data;
    USE_PRIV;

    --(priv->inspect_objects);

#ifdef DEBUG
    syncw_debug ("[recvfs] Stat syncwerk %s.\n", res->obj_id);
#endif

    if (!res->success)
        request_object_batch (processor, priv, res->obj_id);
}

static void
on_fs_write (OSAsyncResult *res, void *cb_data)
{
    CcnetProcessor *processor = cb_data;
    USE_PRIV;

    if (!res->success) {
        syncw_warning ("[recvfs] Failed to write %s.\n", res->obj_id);
        ccnet_processor_send_response (processor, SC_BAD_OBJECT, SS_BAD_OBJECT,
                                       NULL, 0);
        ccnet_processor_done (processor, FALSE);
        return;
    }

    --priv->pending_objects;

#ifdef DEBUG
    syncw_debug ("[recvfs] Wrote fs object %s.\n", res->obj_id);
#endif
}

static int
check_end_condition (CcnetProcessor *processor)
{
    USE_PRIV;

    char *dir_id;
    while (priv->checking_dirs < MAX_CHECKING_DIRS) {
        dir_id = g_queue_pop_head (priv->dir_queue);
        if (!dir_id)
            break;

#ifdef DEBUG
        syncw_debug ("[recvfs] Inspect dir %s.\n", dir_id);
#endif

        if (syncw_obj_store_async_read (syncw->fs_mgr->obj_store,
                                       priv->reader_id,
                                       dir_id) < 0) {
            syncw_warning ("[recvfs] Failed to start async read of %s.\n", dir_id);
            ccnet_processor_send_response (processor, SC_BAD_OBJECT, SS_BAD_OBJECT,
                                           NULL, 0);
            ccnet_processor_done (processor, FALSE);
            return FALSE;
        }
        g_free (dir_id);

        ++(priv->inspect_objects);
        ++(priv->checking_dirs);
    }

    if (priv->checking_dirs > 100)
        syncw_debug ("Number of checking dirs: %d.\n", priv->checking_dirs);
    if (priv->inspect_objects > 1000)
        syncw_debug ("Number of inspect objects: %d.\n", priv->inspect_objects);

    /* Flush periodically. */
    request_object_batch_flush (processor, priv);

    if (priv->pending_objects == 0 && priv->inspect_objects == 0) {
        syncw_debug ("Recv fs end.\n");
        ccnet_processor_send_response (processor, SC_END, SS_END, NULL, 0);
        ccnet_processor_done (processor, TRUE);
        return FALSE;
    } else
        return TRUE;
}

static void
register_async_io (CcnetProcessor *processor)
{
    USE_PRIV;

    priv->registered = TRUE;
    priv->reader_id = syncw_obj_store_register_async_read (syncw->fs_mgr->obj_store,
                                                          priv->store_id,
                                                          priv->repo_version,
                                                          on_syncwdir_read,
                                                          processor);
    priv->stat_id = syncw_obj_store_register_async_stat (syncw->fs_mgr->obj_store,
                                                        priv->store_id,
                                                        priv->repo_version,
                                                        on_syncwerk_stat,
                                                        processor);
    priv->writer_id = syncw_obj_store_register_async_write (syncw->fs_mgr->obj_store,
                                                           priv->store_id,
                                                           priv->repo_version,
                                                           on_fs_write,
                                                           processor);
}

static void *
get_repo_info_thread (void *data)
{
    CcnetProcessor *processor = data;
    USE_PRIV;
    SyncwRepo *repo;

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, priv->repo_id);
    if (!repo) {
        syncw_warning ("Failed to get repo %s.\n", priv->repo_id);
        priv->success = FALSE;
        return data;
    }

    memcpy (priv->store_id, repo->store_id, 36);
    priv->repo_version = repo->version;
    priv->success = TRUE;

    syncw_repo_unref (repo);
    return data;
}

static void
get_repo_info_done (void *data)
{
    CcnetProcessor *processor = data;
    USE_PRIV;

    if (priv->success) {
        ccnet_processor_send_response (processor, SC_OK, SS_OK, NULL, 0);
        processor->state = RECV_ROOT;
        priv->dir_queue = g_queue_new ();
        register_async_io (processor);
    } else {
        ccnet_processor_send_response (processor, SC_SHUTDOWN, SS_SHUTDOWN,
                                       NULL, 0);
        ccnet_processor_done (processor, FALSE);
    }
}

static int
start (CcnetProcessor *processor, int argc, char **argv)
{
    char *session_token;
    USE_PRIV;

    if (argc != 1) {
        ccnet_processor_send_response (processor, SC_BAD_ARGS, SS_BAD_ARGS, NULL, 0);
        ccnet_processor_done (processor, FALSE);
        return -1;
    }

    session_token = argv[0];
    if (syncw_token_manager_verify_token (syncw->token_mgr,
                                         NULL,
                                         processor->peer_id,
                                         session_token, priv->repo_id) == 0) {
        ccnet_processor_thread_create (processor,
                                       syncw->job_mgr,
                                       get_repo_info_thread,
                                       get_repo_info_done,
                                       processor);
        return 0;
    } else {
        ccnet_processor_send_response (processor, 
                                       SC_ACCESS_DENIED, SS_ACCESS_DENIED,
                                       NULL, 0);
        ccnet_processor_done (processor, FALSE);
        return -1;
    }
}

static int
save_fs_object (CcnetProcessor *processor, ObjectPack *pack, int len)
{
    USE_PRIV;

    return syncw_obj_store_async_write (syncw->fs_mgr->obj_store,
                                       priv->writer_id,
                                       pack->id,
                                       pack->object,
                                       len - 41,
                                       FALSE);
}

static int
recv_fs_object (CcnetProcessor *processor, char *content, int clen)
{
    USE_PRIV;
    ObjectPack *pack = (ObjectPack *)content;
    SyncwFSObject *fs_obj = NULL;

    if (clen < sizeof(ObjectPack)) {
        syncw_warning ("invalid object id.\n");
        goto bad;
    }

    syncw_debug ("[recvfs] Recv fs object %.8s.\n", pack->id);

    fs_obj = syncw_fs_object_from_data(pack->id,
                                      pack->object, clen - sizeof(ObjectPack),
                                      (priv->repo_version > 0));
    if (!fs_obj) {
        syncw_warning ("Bad fs object %s.\n", pack->id);
        goto bad;
    }

    if (fs_obj->type == SYNCW_METADATA_TYPE_DIR) {
        SyncwDir *dir = (SyncwDir *)fs_obj;
        int ret = check_syncwdir (processor, dir);
        if (ret < 0)
            goto bad;
    } else if (fs_obj->type == SYNCW_METADATA_TYPE_FILE) {
        /* TODO: check syncwerk format. */
#if 0
        int ret = syncwerk_check_data_format (pack->object, clen - 41);
        if (ret < 0) {
            goto bad;
        }
#endif
    }

    syncw_fs_object_free (fs_obj);

    if (save_fs_object (processor, pack, clen) < 0) {
        goto bad;
    }

    return 0;

bad:
    ccnet_processor_send_response (processor, SC_BAD_OBJECT,
                                   SS_BAD_OBJECT, NULL, 0);
    syncw_warning ("[recvfs] Bad fs object received.\n");
    ccnet_processor_done (processor, FALSE);

    syncw_fs_object_free (fs_obj);

    return -1;
}

static void
recv_fs_object_seg (CcnetProcessor *processor, char *content, int clen)
{
    USE_PRIV;

    /* Append the received object segment to the end */
    priv->obj_seg = g_realloc (priv->obj_seg, priv->obj_seg_len + clen);
    memcpy (priv->obj_seg + priv->obj_seg_len, content, clen);

    syncw_debug ("[recvfs] Get obj seg: <id= %40s, offset= %d, lenth= %d>\n",
                priv->obj_seg, priv->obj_seg_len, clen);

    priv->obj_seg_len += clen;
}

static void
process_fs_object_seg (CcnetProcessor *processor)
{
    USE_PRIV;

    if (recv_fs_object (processor, priv->obj_seg, priv->obj_seg_len) == 0) {
        g_free (priv->obj_seg);
        priv->obj_seg = NULL;
        priv->obj_seg_len = 0;
    }
}

static void
process_fsroot_list (CcnetProcessor *processor)
{
    GList *ptr;
    char *object_id;
    USE_PRIV;

    /* When there are more than one fs roots, there may be many
     * duplicate fs objects between different commits.
     * We remember checked fs objects in a hash table to avoid
     * redundant checks.
     */
    if (priv->n_roots > 1)
        priv->fs_objects = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, NULL);

    request_object_batch_begin (priv);

    for (ptr = priv->fs_roots; ptr != NULL; ptr = ptr->next) {
        object_id = ptr->data;

        /* Empty dir or file always exists. */
        if (strcmp (object_id, EMPTY_SHA1) == 0) {
            object_id += 41;
            continue;
        }

#ifdef DEBUG
        syncw_debug ("[recvfs] Inspect object %s.\n", object_id);
#endif

        g_queue_push_tail (priv->dir_queue, g_strdup(object_id));

        g_free (object_id);
    }

    g_list_free (priv->fs_roots);
    priv->fs_roots = NULL;
}

static void
queue_fs_roots (CcnetProcessor *processor, char *content, int clen)
{
    USE_PRIV;
    char *object_id;
    int n_objects;
    int i;

    if (clen % 41 != 0) {
        syncw_warning ("Bad fs root list.\n");
        ccnet_processor_send_response (processor, SC_BAD_OL, SS_BAD_OL, NULL, 0);
        ccnet_processor_done (processor, FALSE);
        return;
    }

    n_objects = clen/41;
    object_id = content;
    for (i = 0; i < n_objects; ++i) {
        object_id[40] = '\0';
        priv->fs_roots = g_list_prepend (priv->fs_roots, g_strdup(object_id));
        object_id += 41;
        ++(priv->n_roots);
    }

    ccnet_processor_send_response (processor, SC_OK, SS_OK, NULL, 0);
}

static void
handle_update (CcnetProcessor *processor,
               char *code, char *code_msg,
               char *content, int clen)
{
   switch (processor->state) {
   case RECV_ROOT:
        if (strncmp(code, SC_ROOT, 3) == 0) {
            queue_fs_roots (processor, content, clen);
        } else if (strncmp(code, SC_ROOT_END, 3) == 0) {
            /* change state to FETCH_OBJECT */
            process_fsroot_list (processor);
            processor->timer = ccnet_timer_new (
                (TimerCB)check_end_condition, processor, CHECK_INTERVAL);
            processor->state = FETCH_OBJECT;
        } else {
            syncw_warning ("Bad update: %s %s\n", code, code_msg);
            ccnet_processor_send_response (processor,
                                           SC_BAD_UPDATE_CODE, SS_BAD_UPDATE_CODE,
                                           NULL, 0);
            ccnet_processor_done (processor, FALSE);
        }
        break;
    case FETCH_OBJECT:
        if (strncmp(code, SC_OBJ_SEG, 3) == 0) {
            recv_fs_object_seg (processor, content, clen);

        } else if (strncmp(code, SC_OBJ_SEG_END, 3) == 0) {
            recv_fs_object_seg (processor, content, clen);
            process_fs_object_seg (processor);
        } else if (strncmp(code, SC_OBJECT, 3) == 0) {
            recv_fs_object (processor, content, clen);
        } else {
            syncw_warning ("Bad update: %s %s\n", code, code_msg);
            ccnet_processor_send_response (processor,
                                           SC_BAD_UPDATE_CODE, SS_BAD_UPDATE_CODE,
                                           NULL, 0);
            ccnet_processor_done (processor, FALSE);
        }
        break;
    default:
        g_return_if_reached ();
    }
}
