#include "common.h"

#define FUSE_USE_VERSION  26
#include <fuse.h>

#include <glib.h>
#include <glib-object.h>

#include <ccnet.h>
#include <syncwerk-server-db.h>

#include "log.h"
#include "utils.h"

#include "syncwerk-server-fuse.h"

int read_file(SyncwerkSession *syncw,
              const char *store_id, int version,
              Syncwerk *file,
              char *buf, size_t size,
              off_t offset, struct fuse_file_info *info)
{
    BlockHandle *handle = NULL;;
    BlockMetadata *bmd;
    char *blkid;
    char *ptr;
    off_t off = 0, nleft;
    int i, n, ret = -EIO;

    for (i = 0; i < file->n_blocks; i++) {
        blkid = file->blk_sha1s[i];

        bmd = syncw_block_manager_stat_block(syncw->block_mgr, store_id, version, blkid);
        if (!bmd)
            return -EIO;

        if (offset < off + bmd->size) {
            g_free (bmd);
            break;
        }

        off += bmd->size;
        g_free (bmd);
    }

    /* beyond the file size */
    if (i == file->n_blocks)
        return 0;

    nleft = size;
    ptr = buf;
    while (nleft > 0 && i < file->n_blocks) {
        blkid = file->blk_sha1s[i];

        handle = syncw_block_manager_open_block(syncw->block_mgr,
                                               store_id, version,
                                               blkid, BLOCK_READ);
        if (!handle) {
            syncw_warning ("Failed to open block %s:%s.\n", store_id, blkid);
            return -EIO;
        }

        /* trim the offset in a block */
        if (offset > off) {
            char *tmp = (char *)malloc(sizeof(char) * (offset - off));
            if (!tmp)
                return -ENOMEM;

            n = syncw_block_manager_read_block(syncw->block_mgr, handle,
                                              tmp, offset-off);
            if (n != offset - off) {
                syncw_warning ("Failed to read block %s:%s.\n", store_id, blkid);
                free (tmp);
                goto out;
            }

            off += n;
            free(tmp);
        }

        if ((n = syncw_block_manager_read_block(syncw->block_mgr,
                                               handle, ptr, nleft)) < 0) {
            syncw_warning ("Failed to read block %s:%s.\n", store_id, blkid);
            goto out;
        }

        nleft -= n;
        ptr += n;
        off += n;
        ++i;

        /* At this point we should have read all the content of the block or
         * have read up to @size bytes. So it's safe to close the block.
         */
        syncw_block_manager_close_block(syncw->block_mgr, handle);
        syncw_block_manager_block_handle_free (syncw->block_mgr, handle);
    }

    return size - nleft;

out:
    if (handle) {
        syncw_block_manager_close_block(syncw->block_mgr, handle);
        syncw_block_manager_block_handle_free (syncw->block_mgr, handle);
    }
    return ret;
}
