/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include "syncwerk-session.h"
#include "utils.h"
#include "syncwerk-server-utils.h"
#include "block-mgr.h"
#include "log.h"

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <glib/gstdio.h>

#include "block-backend.h"

#define SYNCW_BLOCK_DIR "blocks"


extern BlockBackend *
block_backend_fs_new (const char *block_dir, const char *tmp_dir);


SyncwBlockManager *
syncw_block_manager_new (struct _SyncwerkSession *syncw,
                        const char *syncw_dir)
{
    SyncwBlockManager *mgr;

    mgr = g_new0 (SyncwBlockManager, 1);
    mgr->syncw = syncw;

    mgr->backend = block_backend_fs_new (syncw_dir, syncw->tmp_file_dir);
    if (!mgr->backend) {
        syncw_warning ("[Block mgr] Failed to load backend.\n");
        goto onerror;
    }

    return mgr;

onerror:
    g_free (mgr);

    return NULL;
}

int
syncw_block_manager_init (SyncwBlockManager *mgr)
{
    return 0;
}


BlockHandle *
syncw_block_manager_open_block (SyncwBlockManager *mgr,
                               const char *store_id,
                               int version,
                               const char *block_id,
                               int rw_type)
{
    if (!store_id || !is_uuid_valid(store_id) ||
        !block_id || !is_object_id_valid(block_id))
        return NULL;

    return mgr->backend->open_block (mgr->backend,
                                     store_id, version,
                                     block_id, rw_type);
}

int
syncw_block_manager_read_block (SyncwBlockManager *mgr,
                               BlockHandle *handle,
                               void *buf, int len)
{
    return mgr->backend->read_block (mgr->backend, handle, buf, len);
}

int
syncw_block_manager_write_block (SyncwBlockManager *mgr,
                                BlockHandle *handle,
                                const void *buf, int len)
{
    return mgr->backend->write_block (mgr->backend, handle, buf, len);
}

int
syncw_block_manager_close_block (SyncwBlockManager *mgr,
                                BlockHandle *handle)
{
    return mgr->backend->close_block (mgr->backend, handle);
}

void
syncw_block_manager_block_handle_free (SyncwBlockManager *mgr,
                                      BlockHandle *handle)
{
    return mgr->backend->block_handle_free (mgr->backend, handle);
}

int
syncw_block_manager_commit_block (SyncwBlockManager *mgr,
                                 BlockHandle *handle)
{
    return mgr->backend->commit_block (mgr->backend, handle);
}
    
gboolean syncw_block_manager_block_exists (SyncwBlockManager *mgr,
                                          const char *store_id,
                                          int version,
                                          const char *block_id)
{
    if (!store_id || !is_uuid_valid(store_id) ||
        !block_id || !is_object_id_valid(block_id))
        return FALSE;

    return mgr->backend->exists (mgr->backend, store_id, version, block_id);
}

int
syncw_block_manager_remove_block (SyncwBlockManager *mgr,
                                 const char *store_id,
                                 int version,
                                 const char *block_id)
{
    if (!store_id || !is_uuid_valid(store_id) ||
        !block_id || !is_object_id_valid(block_id))
        return -1;

    return mgr->backend->remove_block (mgr->backend, store_id, version, block_id);
}

BlockMetadata *
syncw_block_manager_stat_block (SyncwBlockManager *mgr,
                               const char *store_id,
                               int version,
                               const char *block_id)
{
    if (!store_id || !is_uuid_valid(store_id) ||
        !block_id || !is_object_id_valid(block_id))
        return NULL;

    return mgr->backend->stat_block (mgr->backend, store_id, version, block_id);
}

BlockMetadata *
syncw_block_manager_stat_block_by_handle (SyncwBlockManager *mgr,
                                         BlockHandle *handle)
{
    return mgr->backend->stat_block_by_handle (mgr->backend, handle);
}

int
syncw_block_manager_foreach_block (SyncwBlockManager *mgr,
                                  const char *store_id,
                                  int version,
                                  SyncwBlockFunc process,
                                  void *user_data)
{
    return mgr->backend->foreach_block (mgr->backend,
                                        store_id, version,
                                        process, user_data);
}

int
syncw_block_manager_copy_block (SyncwBlockManager *mgr,
                               const char *src_store_id,
                               int src_version,
                               const char *dst_store_id,
                               int dst_version,
                               const char *block_id)
{
    if (strcmp (block_id, EMPTY_SHA1) == 0)
        return 0;

    return mgr->backend->copy (mgr->backend,
                               src_store_id,
                               src_version,
                               dst_store_id,
                               dst_version,
                               block_id);
}

static gboolean
get_block_number (const char *store_id,
                  int version,
                  const char *block_id,
                  void *data)
{
    guint64 *n_blocks = data;

    ++(*n_blocks);

    return TRUE;
}

guint64
syncw_block_manager_get_block_number (SyncwBlockManager *mgr,
                                     const char *store_id,
                                     int version)
{
    guint64 n_blocks = 0;

    syncw_block_manager_foreach_block (mgr, store_id, version,
                                      get_block_number, &n_blocks);

    return n_blocks;
}

gboolean
syncw_block_manager_verify_block (SyncwBlockManager *mgr,
                                 const char *store_id,
                                 int version,
                                 const char *block_id,
                                 gboolean *io_error)
{
    BlockHandle *h;
    char buf[10240];
    int n;
    SHA_CTX ctx;
    guint8 sha1[20];
    char check_id[41];

    h = syncw_block_manager_open_block (mgr,
                                       store_id, version,
                                       block_id, BLOCK_READ);
    if (!h) {
        syncw_warning ("Failed to open block %s:%.8s.\n", store_id, block_id);
        *io_error = TRUE;
        return FALSE;
    }

    SHA1_Init (&ctx);
    while (1) {
        n = syncw_block_manager_read_block (mgr, h, buf, sizeof(buf));
        if (n < 0) {
            syncw_warning ("Failed to read block %s:%.8s.\n", store_id, block_id);
            *io_error = TRUE;
            return FALSE;
        }
        if (n == 0)
            break;

        SHA1_Update (&ctx, buf, n);
    }

    syncw_block_manager_close_block (mgr, h);
    syncw_block_manager_block_handle_free (mgr, h);

    SHA1_Final (sha1, &ctx);
    rawdata_to_hex (sha1, check_id, 20);

    if (strcmp (check_id, block_id) == 0)
        return TRUE;
    else
        return FALSE;
}

int
syncw_block_manager_remove_store (SyncwBlockManager *mgr,
                                 const char *store_id)
{
    return mgr->backend->remove_store (mgr->backend, store_id);
}
