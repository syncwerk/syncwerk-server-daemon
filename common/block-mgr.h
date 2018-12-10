/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCW_BLOCK_MGR_H
#define SYNCW_BLOCK_MGR_H

#include <glib.h>
#include <glib-object.h>
#include <stdint.h>

#include "block.h"

struct _SyncwerkSession;

typedef struct _SyncwBlockManager SyncwBlockManager;

struct _SyncwBlockManager {
    struct _SyncwerkSession *syncw;

    struct BlockBackend *backend;
};


SyncwBlockManager *
syncw_block_manager_new (struct _SyncwerkSession *syncw,
                        const char *syncw_dir);

/*
 * Open a block for read or write.
 *
 * @store_id: id for the block store
 * @version: data format version for the repo
 * @block_id: ID of block.
 * @rw_type: BLOCK_READ or BLOCK_WRITE.
 * Returns: A handle for the block.
 */
BlockHandle *
syncw_block_manager_open_block (SyncwBlockManager *mgr,
                               const char *store_id,
                               int version,
                               const char *block_id,
                               int rw_type);

/*
 * Read data from a block.
 * The semantics is similar to readn.
 *
 * @handle: Hanlde returned by syncw_block_manager_open_block().
 * @buf: Data wuold be copied into this buf.
 * @len: At most @len bytes would be read.
 *
 * Returns: the bytes read.
 */
int
syncw_block_manager_read_block (SyncwBlockManager *mgr,
                               BlockHandle *handle,
                               void *buf, int len);

/*
 * Write data to a block.
 * The semantics is similar to writen.
 *
 * @handle: Hanlde returned by syncw_block_manager_open_block().
 * @buf: Data to be written to the block.
 * @len: At most @len bytes would be written.
 *
 * Returns: the bytes written.
 */
int
syncw_block_manager_write_block (SyncwBlockManager *mgr,
                                BlockHandle *handle,
                                const void *buf, int len);

/*
 * Commit a block to storage.
 * The block must be opened for write.
 *
 * @handle: Hanlde returned by syncw_block_manager_open_block().
 *
 * Returns: 0 on success, -1 on error.
 */
int
syncw_block_manager_commit_block (SyncwBlockManager *mgr,
                                 BlockHandle *handle);

/*
 * Close an open block.
 *
 * @handle: Hanlde returned by syncw_block_manager_open_block().
 *
 * Returns: 0 on success, -1 on error.
 */
int
syncw_block_manager_close_block (SyncwBlockManager *mgr,
                                BlockHandle *handle);

void
syncw_block_manager_block_handle_free (SyncwBlockManager *mgr,
                                      BlockHandle *handle);

gboolean 
syncw_block_manager_block_exists (SyncwBlockManager *mgr,
                                 const char *store_id,
                                 int version,
                                 const char *block_id);

int
syncw_block_manager_remove_block (SyncwBlockManager *mgr,
                                 const char *store_id,
                                 int version,
                                 const char *block_id);

BlockMetadata *
syncw_block_manager_stat_block (SyncwBlockManager *mgr,
                               const char *store_id,
                               int version,
                               const char *block_id);

BlockMetadata *
syncw_block_manager_stat_block_by_handle (SyncwBlockManager *mgr,
                                         BlockHandle *handle);

int
syncw_block_manager_foreach_block (SyncwBlockManager *mgr,
                                  const char *store_id,
                                  int version,
                                  SyncwBlockFunc process,
                                  void *user_data);

int
syncw_block_manager_copy_block (SyncwBlockManager *mgr,
                               const char *src_store_id,
                               int src_version,
                               const char *dst_store_id,
                               int dst_version,
                               const char *block_id);

/* Remove all blocks for a repo. Only valid for version 1 repo. */
int
syncw_block_manager_remove_store (SyncwBlockManager *mgr,
                                 const char *store_id);

guint64
syncw_block_manager_get_block_number (SyncwBlockManager *mgr,
                                     const char *store_id,
                                     int version);

gboolean
syncw_block_manager_verify_block (SyncwBlockManager *mgr,
                                 const char *store_id,
                                 int version,
                                 const char *block_id,
                                 gboolean *io_error);

#endif
