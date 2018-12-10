/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <ccnet.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#ifndef WIN32
    #include <arpa/inet.h>
#endif

#include <openssl/sha.h>
#include <rpcsyncwerk-utils.h>

#include "syncwerk-session.h"
#include "syncwerk-error.h"
#include "fs-mgr.h"
#include "block-mgr.h"
#include "utils.h"
#include "syncwerk-server-utils.h"
#define DEBUG_FLAG SYNCWERK_DEBUG_OTHER
#include "log.h"
#include "../common/syncwerk-crypt.h"

#ifndef SYNCWERK_SERVER
#include "../daemon/vc-utils.h"
#include "vc-common.h"
#endif  /* SYNCWERK_SERVER */

#include "db.h"

#define SYNCW_TMP_EXT "~"

struct _SyncwFSManagerPriv {
    /* GHashTable      *syncwerk_cache; */
    GHashTable      *bl_cache;
};

typedef struct SyncwerkOndisk {
    guint32          type;
    guint64          file_size;
    unsigned char    block_ids[0];
} __attribute__((gcc_struct, __packed__)) SyncwerkOndisk;

typedef struct DirentOndisk {
    guint32 mode;
    char    id[40];
    guint32 name_len;
    char    name[0];
} __attribute__((gcc_struct, __packed__)) DirentOndisk;

typedef struct SyncwdirOndisk {
    guint32 type;
    char    dirents[0];
} __attribute__((gcc_struct, __packed__)) SyncwdirOndisk;

#ifndef SYNCWERK_SERVER
uint32_t
calculate_chunk_size (uint64_t total_size);
static int
write_syncwerk (SyncwFSManager *fs_mgr,
               const char *repo_id, int version,
               CDCFileDescriptor *cdc,
               unsigned char *obj_sha1);
#endif  /* SYNCWERK_SERVER */

SyncwFSManager *
syncw_fs_manager_new (SyncwerkSession *syncw,
                     const char *syncw_dir)
{
    SyncwFSManager *mgr = g_new0 (SyncwFSManager, 1);

    mgr->syncw = syncw;

    mgr->obj_store = syncw_obj_store_new (syncw, "fs");
    if (!mgr->obj_store) {
        g_free (mgr);
        return NULL;
    }

    mgr->priv = g_new0(SyncwFSManagerPriv, 1);

    return mgr;
}

int
syncw_fs_manager_init (SyncwFSManager *mgr)
{
#ifdef SYNCWERK_SERVER

#ifdef FULL_FEATURE
    if (syncw_obj_store_init (mgr->obj_store, TRUE, syncw->ev_mgr) < 0) {
        syncw_warning ("[fs mgr] Failed to init fs object store.\n");
        return -1;
    }
#else
    if (syncw_obj_store_init (mgr->obj_store, FALSE, NULL) < 0) {
        syncw_warning ("[fs mgr] Failed to init fs object store.\n");
        return -1;
    }
#endif

#else
    if (syncw_obj_store_init (mgr->obj_store, TRUE, syncw->ev_mgr) < 0) {
        syncw_warning ("[fs mgr] Failed to init fs object store.\n");
        return -1;
    }
#endif

    return 0;
}

#ifndef SYNCWERK_SERVER
static int
checkout_block (const char *repo_id,
                int version,
                const char *block_id,
                int wfd,
                SyncwerkCrypt *crypt)
{
    SyncwBlockManager *block_mgr = syncw->block_mgr;
    BlockHandle *handle;
    BlockMetadata *bmd;
    char *dec_out = NULL;
    int dec_out_len = -1;
    char *blk_content = NULL;

    handle = syncw_block_manager_open_block (block_mgr,
                                            repo_id, version,
                                            block_id, BLOCK_READ);
    if (!handle) {
        syncw_warning ("Failed to open block %s\n", block_id);
        return -1;
    }

    /* first stat the block to get its size */
    bmd = syncw_block_manager_stat_block_by_handle (block_mgr, handle);
    if (!bmd) {
        syncw_warning ("can't stat block %s.\n", block_id);
        goto checkout_blk_error;
    }

    /* empty file, skip it */
    if (bmd->size == 0) {
        syncw_block_manager_close_block (block_mgr, handle);
        syncw_block_manager_block_handle_free (block_mgr, handle);
        return 0;
    }

    blk_content = (char *)malloc (bmd->size * sizeof(char));

    /* read the block to prepare decryption */
    if (syncw_block_manager_read_block (block_mgr, handle,
                                       blk_content, bmd->size) != bmd->size) {
        syncw_warning ("Error when reading from block %s.\n", block_id);
        goto checkout_blk_error;
    }

    if (crypt != NULL) {

        /* An encrypted block size must be a multiple of
           ENCRYPT_BLK_SIZE
        */
        if (bmd->size % ENCRYPT_BLK_SIZE != 0) {
            syncw_warning ("Error: An invalid encrypted block, %s \n", block_id);
            goto checkout_blk_error;
        }

        /* decrypt the block */
        int ret = syncwerk_decrypt (&dec_out,
                                   &dec_out_len,
                                   blk_content,
                                   bmd->size,
                                   crypt);

        if (ret != 0) {
            syncw_warning ("Decryt block %s failed. \n", block_id);
            goto checkout_blk_error;
        }

        /* write the decrypted content */
        ret = writen (wfd, dec_out, dec_out_len);


        if (ret !=  dec_out_len) {
            syncw_warning ("Failed to write the decryted block %s.\n",
                       block_id);
            goto checkout_blk_error;
        }

        g_free (blk_content);
        g_free (dec_out);

    } else {
        /* not an encrypted block */
        if (writen(wfd, blk_content, bmd->size) != bmd->size) {
            syncw_warning ("Failed to write the decryted block %s.\n",
                       block_id);
            goto checkout_blk_error;
        }
        g_free (blk_content);
    }

    g_free (bmd);
    syncw_block_manager_close_block (block_mgr, handle);
    syncw_block_manager_block_handle_free (block_mgr, handle);
    return 0;

checkout_blk_error:

    if (blk_content)
        free (blk_content);
    if (dec_out)
        g_free (dec_out);
    if (bmd)
        g_free (bmd);

    syncw_block_manager_close_block (block_mgr, handle);
    syncw_block_manager_block_handle_free (block_mgr, handle);
    return -1;
}

int
syncw_fs_manager_checkout_file (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *file_id,
                               const char *file_path,
                               guint32 mode,
                               guint64 mtime,
                               SyncwerkCrypt *crypt,
                               const char *in_repo_path,
                               const char *conflict_head_id,
                               gboolean force_conflict,
                               gboolean *conflicted,
                               const char *email)
{
    Syncwerk *syncwerk;
    char *blk_id;
    int wfd;
    int i;
    char *tmp_path;
    char *conflict_path;

    *conflicted = FALSE;

    syncwerk = syncw_fs_manager_get_syncwerk (mgr, repo_id, version, file_id);
    if (!syncwerk) {
        syncw_warning ("File %s does not exist.\n", file_id);
        return -1;
    }

    tmp_path = g_strconcat (file_path, SYNCW_TMP_EXT, NULL);

    mode_t rmode = mode & 0100 ? 0777 : 0666;
    wfd = syncw_util_create (tmp_path, O_WRONLY | O_TRUNC | O_CREAT | O_BINARY,
                            rmode & ~S_IFMT);
    if (wfd < 0) {
        syncw_warning ("Failed to open file %s for checkout: %s.\n",
                   tmp_path, strerror(errno));
        goto bad;
    }

    for (i = 0; i < syncwerk->n_blocks; ++i) {
        blk_id = syncwerk->blk_sha1s[i];
        if (checkout_block (repo_id, version, blk_id, wfd, crypt) < 0)
            goto bad;
    }

    close (wfd);
    wfd = -1;

    if (force_conflict || syncw_util_rename (tmp_path, file_path) < 0) {
        *conflicted = TRUE;

        /* XXX
         * In new syncing protocol and http sync, files are checked out before
         * the repo is created. So we can't get user email from repo at this point.
         * So a email parameter is needed.
         * For old syncing protocol, repo always exists when files are checked out.
         * This is a quick and dirty hack. A cleaner solution should modifiy the
         * code of old syncing protocol to pass in email too. But I don't want to
         * spend more time on the nearly obsoleted code.
         */
        const char *suffix = NULL;
        if (email) {
            suffix = email;
        } else {
            SyncwRepo *repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
            if (!repo)
                goto bad;
            suffix = email;
        }

        conflict_path = gen_conflict_path (file_path, suffix, (gint64)time(NULL));

        syncw_warning ("Cannot update %s, creating conflict file %s.\n",
                      file_path, conflict_path);

        /* First try to rename the local version to a conflict file,
         * this will preserve the version from the server.
         * If this fails, fall back to checking out the server version
         * to the conflict file.
         */
        if (syncw_util_rename (file_path, conflict_path) == 0) {
            if (syncw_util_rename (tmp_path, file_path) < 0) {
                g_free (conflict_path);
                goto bad;
            }
        } else {
            g_free (conflict_path);
            conflict_path = gen_conflict_path_wrapper (repo_id, version,
                                                       conflict_head_id, in_repo_path,
                                                       file_path);
            if (!conflict_path)
                goto bad;

            if (syncw_util_rename (tmp_path, conflict_path) < 0) {
                g_free (conflict_path);
                goto bad;
            }
        }

        g_free (conflict_path);
    }

    if (mtime > 0) {
        /* 
         * Set the checked out file mtime to what it has to be.
         */
        if (syncw_set_file_time (file_path, mtime) < 0) {
            syncw_warning ("Failed to set mtime for %s.\n", file_path);
        }
    }

    g_free (tmp_path);
    syncwerk_unref (syncwerk);
    return 0;

bad:
    if (wfd >= 0)
        close (wfd);
    /* Remove the tmp file if it still exists, in case that rename fails. */
    syncw_util_unlink (tmp_path);
    g_free (tmp_path);
    syncwerk_unref (syncwerk);
    return -1;
}

#endif /* SYNCWERK_SERVER */

static void *
create_syncwerk_v0 (CDCFileDescriptor *cdc, int *ondisk_size, char *syncwerk_id)
{
    SyncwerkOndisk *ondisk;

    rawdata_to_hex (cdc->file_sum, syncwerk_id, 20);

    *ondisk_size = sizeof(SyncwerkOndisk) + cdc->block_nr * 20;
    ondisk = (SyncwerkOndisk *)g_new0 (char, *ondisk_size);

    ondisk->type = htonl(SYNCW_METADATA_TYPE_FILE);
    ondisk->file_size = hton64 (cdc->file_size);
    memcpy (ondisk->block_ids, cdc->blk_sha1s, cdc->block_nr * 20);

    return ondisk;
}

static void *
create_syncwerk_json (int repo_version,
                     CDCFileDescriptor *cdc,
                     int *ondisk_size,
                     char *syncwerk_id)
{
    json_t *object, *block_id_array;

    object = json_object ();

    json_object_set_int_member (object, "type", SYNCW_METADATA_TYPE_FILE);
    json_object_set_int_member (object, "version",
                                syncwerk_version_from_repo_version(repo_version));

    json_object_set_int_member (object, "size", cdc->file_size);

    block_id_array = json_array ();
    int i;
    uint8_t *ptr = cdc->blk_sha1s;
    char block_id[41];
    for (i = 0; i < cdc->block_nr; ++i) {
        rawdata_to_hex (ptr, block_id, 20);
        json_array_append_new (block_id_array, json_string(block_id));
        ptr += 20;
    }
    json_object_set_new (object, "block_ids", block_id_array);

    char *data = json_dumps (object, JSON_SORT_KEYS);
    *ondisk_size = strlen(data);

    /* The syncwerk object id is sha1 hash of the json object. */
    unsigned char sha1[20];
    calculate_sha1 (sha1, data, *ondisk_size);
    rawdata_to_hex (sha1, syncwerk_id, 20);

    json_decref (object);
    return data;
}

void
syncw_fs_manager_calculate_syncwerk_id_json (int repo_version,
                                           CDCFileDescriptor *cdc,
                                           guint8 *file_id_sha1)
{
    json_t *object, *block_id_array;

    object = json_object ();

    json_object_set_int_member (object, "type", SYNCW_METADATA_TYPE_FILE);
    json_object_set_int_member (object, "version",
                                syncwerk_version_from_repo_version(repo_version));

    json_object_set_int_member (object, "size", cdc->file_size);

    block_id_array = json_array ();
    int i;
    uint8_t *ptr = cdc->blk_sha1s;
    char block_id[41];
    for (i = 0; i < cdc->block_nr; ++i) {
        rawdata_to_hex (ptr, block_id, 20);
        json_array_append_new (block_id_array, json_string(block_id));
        ptr += 20;
    }
    json_object_set_new (object, "block_ids", block_id_array);

    char *data = json_dumps (object, JSON_SORT_KEYS);
    int ondisk_size = strlen(data);

    /* The syncwerk object id is sha1 hash of the json object. */
    calculate_sha1 (file_id_sha1, data, ondisk_size);

    json_decref (object);
    free (data);
}

static int
write_syncwerk (SyncwFSManager *fs_mgr,
               const char *repo_id,
               int version,
               CDCFileDescriptor *cdc,
               unsigned char *obj_sha1)
{
    int ret = 0;
    char syncwerk_id[41];
    void *ondisk;
    int ondisk_size;

    if (version > 0) {
        ondisk = create_syncwerk_json (version, cdc, &ondisk_size, syncwerk_id);

        guint8 *compressed;
        int outlen;

        if (syncw_compress (ondisk, ondisk_size, &compressed, &outlen) < 0) {
            syncw_warning ("Failed to compress syncwerk obj %s:%s.\n",
                          repo_id, syncwerk_id);
            ret = -1;
            free (ondisk);
            goto out;
        }

        if (syncw_obj_store_write_obj (fs_mgr->obj_store, repo_id, version, syncwerk_id,
                                      compressed, outlen, FALSE) < 0)
            ret = -1;
        g_free (compressed);
        free (ondisk);
    } else {
        ondisk = create_syncwerk_v0 (cdc, &ondisk_size, syncwerk_id);

        if (syncw_obj_store_write_obj (fs_mgr->obj_store, repo_id, version, syncwerk_id,
                                      ondisk, ondisk_size, FALSE) < 0)
            ret = -1;
        g_free (ondisk);
    }

out:
    if (ret == 0)
        hex_to_rawdata (syncwerk_id, obj_sha1, 20);

    return ret;
}

uint32_t
calculate_chunk_size (uint64_t total_size)
{
    const uint64_t GiB = 1073741824;
    const uint64_t MiB = 1048576;

    if (total_size >= (8 * GiB)) return 8 * MiB;
    if (total_size >= (4 * GiB)) return 4 * MiB;
    if (total_size >= (2 * GiB)) return 2 * MiB;

    return 1 * MiB;
}

static int
do_write_chunk (const char *repo_id, int version,
                uint8_t *checksum, const char *buf, int len)
{
    SyncwBlockManager *blk_mgr = syncw->block_mgr;
    char chksum_str[41];
    BlockHandle *handle;
    int n;

    rawdata_to_hex (checksum, chksum_str, 20);

    /* Don't write if the block already exists. */
    if (syncw_block_manager_block_exists (syncw->block_mgr,
                                         repo_id, version,
                                         chksum_str))
        return 0;

    handle = syncw_block_manager_open_block (blk_mgr,
                                            repo_id, version,
                                            chksum_str, BLOCK_WRITE);
    if (!handle) {
        syncw_warning ("Failed to open block %s.\n", chksum_str);
        return -1;
    }

    n = syncw_block_manager_write_block (blk_mgr, handle, buf, len);
    if (n < 0) {
        syncw_warning ("Failed to write chunk %s.\n", chksum_str);
        syncw_block_manager_close_block (blk_mgr, handle);
        syncw_block_manager_block_handle_free (blk_mgr, handle);
        return -1;
    }

    if (syncw_block_manager_close_block (blk_mgr, handle) < 0) {
        syncw_warning ("failed to close block %s.\n", chksum_str);
        syncw_block_manager_block_handle_free (blk_mgr, handle);
        return -1;
    }

    if (syncw_block_manager_commit_block (blk_mgr, handle) < 0) {
        syncw_warning ("failed to commit chunk %s.\n", chksum_str);
        syncw_block_manager_block_handle_free (blk_mgr, handle);
        return -1;
    }

    syncw_block_manager_block_handle_free (blk_mgr, handle);
    return 0;
}

/* write the chunk and store its checksum */
int
syncwerk_write_chunk (const char *repo_id,
                     int version,
                     CDCDescriptor *chunk,
                     SyncwerkCrypt *crypt,
                     uint8_t *checksum,
                     gboolean write_data)
{
    SHA_CTX ctx;
    int ret = 0;

    /* Encrypt before write to disk if needed, and we don't encrypt
     * empty files. */
    if (crypt != NULL && chunk->len) {
        char *encrypted_buf = NULL;         /* encrypted output */
        int enc_len = -1;                /* encrypted length */

        ret = syncwerk_encrypt (&encrypted_buf, /* output */
                               &enc_len,      /* output len */
                               chunk->block_buf, /* input */
                               chunk->len,       /* input len */
                               crypt);
        if (ret != 0) {
            syncw_warning ("Error: failed to encrypt block\n");
            return -1;
        }

        SHA1_Init (&ctx);
        SHA1_Update (&ctx, encrypted_buf, enc_len);
        SHA1_Final (checksum, &ctx);

        if (write_data)
            ret = do_write_chunk (repo_id, version, checksum, encrypted_buf, enc_len);
        g_free (encrypted_buf);
    } else {
        /* not a encrypted repo, go ahead */
        SHA1_Init (&ctx);
        SHA1_Update (&ctx, chunk->block_buf, chunk->len);
        SHA1_Final (checksum, &ctx);

        if (write_data)
            ret = do_write_chunk (repo_id, version, checksum, chunk->block_buf, chunk->len);
    }

    return ret;
}

static void
create_cdc_for_empty_file (CDCFileDescriptor *cdc)
{
    memset (cdc, 0, sizeof(CDCFileDescriptor));
}

#if defined SYNCWERK_SERVER && defined FULL_FEATURE

#define FIXED_BLOCK_SIZE (1<<20)

typedef struct ChunkingData {
    const char *repo_id;
    int version;
    const char *file_path;
    SyncwerkCrypt *crypt;
    guint8 *blk_sha1s;
    GAsyncQueue *finished_tasks;
} ChunkingData;

static void
chunking_worker (gpointer vdata, gpointer user_data)
{
    ChunkingData *data = user_data;
    CDCDescriptor *chunk = vdata;
    int fd = -1;
    ssize_t n;
    int idx;

    chunk->block_buf = g_new0 (char, chunk->len);
    if (!chunk->block_buf) {
        syncw_warning ("Failed to allow chunk buffer\n");
        goto out;
    }

    fd = syncw_util_open (data->file_path, O_RDONLY | O_BINARY);
    if (fd < 0) {
        syncw_warning ("Failed to open %s: %s\n", data->file_path, strerror(errno));
        chunk->result = -1;
        goto out;
    }

    if (syncw_util_lseek (fd, chunk->offset, SEEK_SET) == (gint64)-1) {
        syncw_warning ("Failed to lseek %s: %s\n", data->file_path, strerror(errno));
        chunk->result = -1;
        goto out;
    }

    n = readn (fd, chunk->block_buf, chunk->len);
    if (n < 0) {
        syncw_warning ("Failed to read chunk from %s: %s\n",
                      data->file_path, strerror(errno));
        chunk->result = -1;
        goto out;
    }

    chunk->result = syncwerk_write_chunk (data->repo_id, data->version,
                                         chunk, data->crypt,
                                         chunk->checksum, 1);
    if (chunk->result < 0)
        goto out;

    idx = chunk->offset / syncw->http_server->fixed_block_size;
    memcpy (data->blk_sha1s + idx * CHECKSUM_LENGTH, chunk->checksum, CHECKSUM_LENGTH);

out:
    g_free (chunk->block_buf);
    close (fd);
    g_async_queue_push (data->finished_tasks, chunk);
}

static int
split_file_to_block (const char *repo_id,
                     int version,
                     const char *file_path,
                     gint64 file_size,
                     SyncwerkCrypt *crypt,
                     CDCFileDescriptor *cdc,
                     gboolean write_data,
                     gint64 *indexed)
{
    int n_blocks;
    uint8_t *block_sha1s = NULL;
    GThreadPool *tpool = NULL;
    GAsyncQueue *finished_tasks = NULL;
    GList *pending_tasks = NULL;
    int n_pending = 0;
    CDCDescriptor *chunk;
    int ret = 0;

    n_blocks = (file_size + syncw->http_server->fixed_block_size - 1) / syncw->http_server->fixed_block_size;
    block_sha1s = g_new0 (uint8_t, n_blocks * CHECKSUM_LENGTH);
    if (!block_sha1s) {
        syncw_warning ("Failed to allocate block_sha1s.\n");
        ret = -1;
        goto out;
    }

    finished_tasks = g_async_queue_new ();

    ChunkingData data;
    memset (&data, 0, sizeof(data));
    data.repo_id = repo_id;
    data.version = version;
    data.file_path = file_path;
    data.crypt = crypt;
    data.blk_sha1s = block_sha1s;
    data.finished_tasks = finished_tasks;

    tpool = g_thread_pool_new (chunking_worker, &data,
                               syncw->http_server->max_indexing_threads, FALSE, NULL);
    if (!tpool) {
        syncw_warning ("Failed to allocate thread pool\n");
        ret = -1;
        goto out;
    }

    guint64 offset = 0;
    guint64 len;
    guint64 left = (guint64)file_size;
    while (left > 0) {
        len = ((left >= syncw->http_server->fixed_block_size) ? syncw->http_server->fixed_block_size : left);

        chunk = g_new0 (CDCDescriptor, 1);
        chunk->offset = offset;
        chunk->len = (guint32)len;

        g_thread_pool_push (tpool, chunk, NULL);
        pending_tasks = g_list_prepend (pending_tasks, chunk);
        n_pending++;

        left -= len;
        offset += len;
    }

    while ((chunk = g_async_queue_pop (finished_tasks)) != NULL) {
        if (chunk->result < 0) {
            ret = -1;
            goto out;
        }
        if (indexed)
            *indexed += syncw->http_server->fixed_block_size;

        if ((--n_pending) <= 0) {
            if (indexed)
                *indexed = (guint64)file_size;
            break;
        }
    }

    cdc->block_nr = n_blocks;
    cdc->blk_sha1s = block_sha1s;

out:
    if (tpool)
        g_thread_pool_free (tpool, TRUE, TRUE);
    if (finished_tasks)
        g_async_queue_unref (finished_tasks);
    g_list_free_full (pending_tasks, g_free);
    if (ret < 0)
        g_free (block_sha1s);

    return ret;
}

#endif  /* SYNCWERK_SERVER */

#define CDC_AVERAGE_BLOCK_SIZE (1 << 23) /* 8MB */
#define CDC_MIN_BLOCK_SIZE (6 * (1 << 20)) /* 6MB */
#define CDC_MAX_BLOCK_SIZE (10 * (1 << 20)) /* 10MB */

int
syncw_fs_manager_index_blocks (SyncwFSManager *mgr,
                              const char *repo_id,
                              int version,
                              const char *file_path,
                              unsigned char sha1[],
                              gint64 *size,
                              SyncwerkCrypt *crypt,
                              gboolean write_data,
                              gboolean use_cdc,
                              gint64 *indexed)
{
    SyncwStat sb;
    CDCFileDescriptor cdc;

    if (syncw_stat (file_path, &sb) < 0) {
        syncw_warning ("Bad file %s: %s.\n", file_path, strerror(errno));
        return -1;
    }

    g_return_val_if_fail (S_ISREG(sb.st_mode), -1);

    if (sb.st_size == 0) {
        /* handle empty file. */
        memset (sha1, 0, 20);
        create_cdc_for_empty_file (&cdc);
    } else {
        memset (&cdc, 0, sizeof(cdc));
#if defined SYNCWERK_SERVER && defined FULL_FEATURE
        if (use_cdc || version == 0) {
            cdc.block_sz = CDC_AVERAGE_BLOCK_SIZE;
            cdc.block_min_sz = CDC_MIN_BLOCK_SIZE;
            cdc.block_max_sz = CDC_MAX_BLOCK_SIZE;
            cdc.write_block = syncwerk_write_chunk;
            memcpy (cdc.repo_id, repo_id, 36);
            cdc.version = version;
            if (filename_chunk_cdc (file_path, &cdc, crypt, write_data, indexed) < 0) {
                syncw_warning ("Failed to chunk file with CDC.\n");
                return -1;
            }
        } else {
            memcpy (cdc.repo_id, repo_id, 36);
            cdc.version = version;
            cdc.file_size = sb.st_size;
            if (split_file_to_block (repo_id, version, file_path, sb.st_size,
                                     crypt, &cdc, write_data, indexed) < 0) {
                return -1;
            }
        }
#else
        cdc.block_sz = CDC_AVERAGE_BLOCK_SIZE;
        cdc.block_min_sz = CDC_MIN_BLOCK_SIZE;
        cdc.block_max_sz = CDC_MAX_BLOCK_SIZE;
        cdc.write_block = syncwerk_write_chunk;
        memcpy (cdc.repo_id, repo_id, 36);
        cdc.version = version;
        if (filename_chunk_cdc (file_path, &cdc, crypt, write_data, indexed) < 0) {
            syncw_warning ("Failed to chunk file with CDC.\n");
            return -1;
        }
#endif

        if (write_data && write_syncwerk (mgr, repo_id, version, &cdc, sha1) < 0) {
            g_free (cdc.blk_sha1s);
            syncw_warning ("Failed to write syncwerk for %s.\n", file_path);
            return -1;
        }
    }

    *size = (gint64)sb.st_size;

    if (cdc.blk_sha1s)
        free (cdc.blk_sha1s);

    return 0;
}

static int
check_and_write_block (const char *repo_id, int version,
                       const char *path, unsigned char *sha1, const char *block_id)
{
    char *content;
    gsize len;
    GError *error = NULL;
    int ret = 0;

    if (!g_file_get_contents (path, &content, &len, &error)) {
        if (error) {
            syncw_warning ("Failed to read %s: %s.\n", path, error->message);
            g_clear_error (&error);
            return -1;
        }
    }

    SHA_CTX block_ctx;
    unsigned char checksum[20];

    SHA1_Init (&block_ctx);
    SHA1_Update (&block_ctx, content, len);
    SHA1_Final (checksum, &block_ctx);

    if (memcmp (checksum, sha1, 20) != 0) {
        syncw_warning ("Block id %s:%s doesn't match content.\n", repo_id, block_id);
        ret = -1;
        goto out;
    }

    if (do_write_chunk (repo_id, version, sha1, content, len) < 0) {
        ret = -1;
        goto out;
    }

out:
    g_free (content);
    return ret;
}

static int
check_and_write_file_blocks (CDCFileDescriptor *cdc, GList *paths, GList *blockids)
{
    GList *ptr, *q;
    SHA_CTX file_ctx;
    int ret = 0;

    SHA1_Init (&file_ctx);
    for (ptr = paths, q = blockids; ptr; ptr = ptr->next, q = q->next) {
        char *path = ptr->data;
        char *blk_id = q->data;
        unsigned char sha1[20];

        hex_to_rawdata (blk_id, sha1, 20);
        ret = check_and_write_block (cdc->repo_id, cdc->version, path, sha1, blk_id);
        if (ret < 0)
            goto out;

        memcpy (cdc->blk_sha1s + cdc->block_nr * CHECKSUM_LENGTH,
                sha1, CHECKSUM_LENGTH);
        cdc->block_nr++;

        SHA1_Update (&file_ctx, sha1, 20);
    }

    SHA1_Final (cdc->file_sum, &file_ctx);

out:
    return ret;
}

static int
check_existed_file_blocks (CDCFileDescriptor *cdc, GList *blockids)
{
    GList *q;
    SHA_CTX file_ctx;
    int ret = 0;

    SHA1_Init (&file_ctx);
    for (q = blockids; q; q = q->next) {
        char *blk_id = q->data;
        unsigned char sha1[20];

        if (!syncw_block_manager_block_exists (
                syncw->block_mgr, cdc->repo_id, cdc->version, blk_id)) {
            ret = -1;
            goto out;
        }

        hex_to_rawdata (blk_id, sha1, 20);
        memcpy (cdc->blk_sha1s + cdc->block_nr * CHECKSUM_LENGTH,
                sha1, CHECKSUM_LENGTH);
        cdc->block_nr++;

        SHA1_Update (&file_ctx, sha1, 20);
    }

    SHA1_Final (cdc->file_sum, &file_ctx);

out:
    return ret;
}

static int
init_file_cdc (CDCFileDescriptor *cdc,
               const char *repo_id, int version,
               int block_nr, gint64 file_size)
{
    memset (cdc, 0, sizeof(CDCFileDescriptor));

    cdc->file_size = file_size;

    cdc->blk_sha1s =  (uint8_t *)calloc (sizeof(uint8_t), block_nr * CHECKSUM_LENGTH);
    if (!cdc->blk_sha1s) {
        syncw_warning ("Failed to alloc block sha1 array.\n");
        return -1;
    }

    memcpy (cdc->repo_id, repo_id, 36);
    cdc->version = version;

    return 0;
}

int
syncw_fs_manager_index_file_blocks (SyncwFSManager *mgr,
                                   const char *repo_id,
                                   int version,
                                   GList *paths,
                                   GList *blockids,
                                   unsigned char sha1[],
                                   gint64 file_size)
{
    int ret = 0;
    CDCFileDescriptor cdc;

    if (!paths) {
        /* handle empty file. */
        memset (sha1, 0, 20);
        create_cdc_for_empty_file (&cdc);
    } else {
        int block_nr = g_list_length (paths);

        if (init_file_cdc (&cdc, repo_id, version, block_nr, file_size) < 0) {
            ret = -1;
            goto out;
        }

        if (check_and_write_file_blocks (&cdc, paths, blockids) < 0) {
            syncw_warning ("Failed to check and write file blocks.\n");
            ret = -1;
            goto out;
        }

        if (write_syncwerk (mgr, repo_id, version, &cdc, sha1) < 0) {
            syncw_warning ("Failed to write syncwerk.\n");
            ret = -1;
            goto out;
        }
    }

out:
    if (cdc.blk_sha1s)
        free (cdc.blk_sha1s);

    return ret;
}

int
syncw_fs_manager_index_raw_blocks (SyncwFSManager *mgr,
                                  const char *repo_id,
                                  int version,
                                  GList *paths,
                                  GList *blockids)
{
    int ret = 0;
    GList *ptr, *q;

    if (!paths)
        return -1;

    for (ptr = paths, q = blockids; ptr; ptr = ptr->next, q = q->next) {
        char *path = ptr->data;
        char *blk_id = q->data;
        unsigned char sha1[20];

        hex_to_rawdata (blk_id, sha1, 20);
        ret = check_and_write_block (repo_id, version, path, sha1, blk_id);
        if (ret < 0)
            break;

    }

    return ret;
}

int
syncw_fs_manager_index_existed_file_blocks (SyncwFSManager *mgr,
                                           const char *repo_id,
                                           int version,
                                           GList *blockids,
                                           unsigned char sha1[],
                                           gint64 file_size)
{
    int ret = 0;
    CDCFileDescriptor cdc;

    int block_nr = g_list_length (blockids);
    if (block_nr == 0) {
        /* handle empty file. */
        memset (sha1, 0, 20);
        create_cdc_for_empty_file (&cdc);
    } else {
        if (init_file_cdc (&cdc, repo_id, version, block_nr, file_size) < 0) {
            ret = -1;
            goto out;
        }

        if (check_existed_file_blocks (&cdc, blockids) < 0) {
            syncw_warning ("Failed to check and write file blocks.\n");
            ret = -1;
            goto out;
        }

        if (write_syncwerk (mgr, repo_id, version, &cdc, sha1) < 0) {
            syncw_warning ("Failed to write syncwerk.\n");
            ret = -1;
            goto out;
        }
    }

out:
    if (cdc.blk_sha1s)
        free (cdc.blk_sha1s);

    return ret;
}

void
syncwerk_ref (Syncwerk *syncwerk)
{
    ++syncwerk->ref_count;
}

static void
syncwerk_free (Syncwerk *syncwerk)
{
    int i;

    if (syncwerk->blk_sha1s) {
        for (i = 0; i < syncwerk->n_blocks; ++i)
            g_free (syncwerk->blk_sha1s[i]);
        g_free (syncwerk->blk_sha1s);
    }

    g_free (syncwerk);
}

void
syncwerk_unref (Syncwerk *syncwerk)
{
    if (!syncwerk)
        return;

    if (--syncwerk->ref_count <= 0)
        syncwerk_free (syncwerk);
}

static Syncwerk *
syncwerk_from_v0_data (const char *id, const void *data, int len)
{
    const SyncwerkOndisk *ondisk = data;
    Syncwerk *syncwerk;
    int id_list_len, n_blocks;

    if (len < sizeof(SyncwerkOndisk)) {
        syncw_warning ("[fs mgr] Corrupt syncwerk object %s.\n", id);
        return NULL;
    }

    if (ntohl(ondisk->type) != SYNCW_METADATA_TYPE_FILE) {
        syncw_warning ("[fd mgr] %s is not a file.\n", id);
        return NULL;
    }

    id_list_len = len - sizeof(SyncwerkOndisk);
    if (id_list_len % 20 != 0) {
        syncw_warning ("[fs mgr] Corrupt syncwerk object %s.\n", id);
        return NULL;
    }
    n_blocks = id_list_len / 20;

    syncwerk = g_new0 (Syncwerk, 1);

    syncwerk->object.type = SYNCW_METADATA_TYPE_FILE;
    syncwerk->version = 0;
    memcpy (syncwerk->file_id, id, 41);
    syncwerk->file_size = ntoh64 (ondisk->file_size);
    syncwerk->n_blocks = n_blocks;

    syncwerk->blk_sha1s = g_new0 (char*, syncwerk->n_blocks);
    const unsigned char *blk_sha1_ptr = ondisk->block_ids;
    int i;
    for (i = 0; i < syncwerk->n_blocks; ++i) {
        char *blk_sha1 = g_new0 (char, 41);
        syncwerk->blk_sha1s[i] = blk_sha1;
        rawdata_to_hex (blk_sha1_ptr, blk_sha1, 20);
        blk_sha1_ptr += 20;
    }

    syncwerk->ref_count = 1;
    return syncwerk;
}

static Syncwerk *
syncwerk_from_json_object (const char *id, json_t *object)
{
    json_t *block_id_array = NULL;
    int type;
    int version;
    guint64 file_size;
    Syncwerk *syncwerk = NULL;

    /* Sanity checks. */
    type = json_object_get_int_member (object, "type");
    if (type != SYNCW_METADATA_TYPE_FILE) {
        syncw_debug ("Object %s is not a file.\n", id);
        return NULL;
    }

    version = (int) json_object_get_int_member (object, "version");
    if (version < 1) {
        syncw_debug ("Syncwerk object %s version should be > 0, version is %d.\n",
                    id, version);
        return NULL;
    }

    file_size = (guint64) json_object_get_int_member (object, "size");

    block_id_array = json_object_get (object, "block_ids");
    if (!block_id_array) {
        syncw_debug ("No block id array in syncwerk object %s.\n", id);
        return NULL;
    }

    syncwerk = g_new0 (Syncwerk, 1);

    syncwerk->object.type = SYNCW_METADATA_TYPE_FILE;

    memcpy (syncwerk->file_id, id, 40);
    syncwerk->version = version;
    syncwerk->file_size = file_size;
    syncwerk->n_blocks = json_array_size (block_id_array);
    syncwerk->blk_sha1s = g_new0 (char *, syncwerk->n_blocks);

    int i;
    json_t *block_id_obj;
    const char *block_id;
    for (i = 0; i < syncwerk->n_blocks; ++i) {
        block_id_obj = json_array_get (block_id_array, i);
        block_id = json_string_value (block_id_obj);
        if (!block_id || !is_object_id_valid(block_id)) {
            syncwerk_free (syncwerk);
            return NULL;
        }
        syncwerk->blk_sha1s[i] = g_strdup(block_id);
    }

    syncwerk->ref_count = 1;

    return syncwerk;
}

static Syncwerk *
syncwerk_from_json (const char *id, void *data, int len)
{
    guint8 *decompressed;
    int outlen;
    json_t *object = NULL;
    json_error_t error;
    Syncwerk *syncwerk;

    if (syncw_decompress (data, len, &decompressed, &outlen) < 0) {
        syncw_warning ("Failed to decompress syncwerk object %s.\n", id);
        return NULL;
    }

    object = json_loadb ((const char *)decompressed, outlen, 0, &error);
    g_free (decompressed);
    if (!object) {
        if (error.text)
            syncw_warning ("Failed to load syncwerk json object: %s.\n", error.text);
        else
            syncw_warning ("Failed to load syncwerk json object.\n");
        return NULL;
    }

    syncwerk = syncwerk_from_json_object (id, object);

    json_decref (object);
    return syncwerk;
}

static Syncwerk *
syncwerk_from_data (const char *id, void *data, int len, gboolean is_json)
{
    if (is_json)
        return syncwerk_from_json (id, data, len);
    else
        return syncwerk_from_v0_data (id, data, len);
}

Syncwerk *
syncw_fs_manager_get_syncwerk (SyncwFSManager *mgr,
                             const char *repo_id,
                             int version,
                             const char *file_id)
{
    void *data;
    int len;
    Syncwerk *syncwerk;

#if 0
    syncwerk = g_hash_table_lookup (mgr->priv->syncwerk_cache, file_id);
    if (syncwerk) {
        syncwerk_ref (syncwerk);
        return syncwerk;
    }
#endif

    if (memcmp (file_id, EMPTY_SHA1, 40) == 0) {
        syncwerk = g_new0 (Syncwerk, 1);
        memset (syncwerk->file_id, '0', 40);
        syncwerk->ref_count = 1;
        return syncwerk;
    }

    if (syncw_obj_store_read_obj (mgr->obj_store, repo_id, version,
                                 file_id, &data, &len) < 0) {
        syncw_warning ("[fs mgr] Failed to read file %s.\n", file_id);
        return NULL;
    }

    syncwerk = syncwerk_from_data (file_id, data, len, (version > 0));
    g_free (data);

#if 0
    /*
     * Add to cache. Also increase ref count.
     */
    syncwerk_ref (syncwerk);
    g_hash_table_insert (mgr->priv->syncwerk_cache, g_strdup(file_id), syncwerk);
#endif

    return syncwerk;
}

static guint8 *
syncwerk_to_v0_data (Syncwerk *file, int *len)
{
    SyncwerkOndisk *ondisk;

    *len = sizeof(SyncwerkOndisk) + file->n_blocks * 20;
    ondisk = (SyncwerkOndisk *)g_new0 (char, *len);

    ondisk->type = htonl(SYNCW_METADATA_TYPE_FILE);
    ondisk->file_size = hton64 (file->file_size);

    guint8 *ptr = ondisk->block_ids;
    int i;
    for (i = 0; i < file->n_blocks; ++i) {
        hex_to_rawdata (file->blk_sha1s[i], ptr, 20);
        ptr += 20;
    }

    return (guint8 *)ondisk;
}

static guint8 *
syncwerk_to_json (Syncwerk *file, int *len)
{
    json_t *object, *block_id_array;

    object = json_object ();

    json_object_set_int_member (object, "type", SYNCW_METADATA_TYPE_FILE);
    json_object_set_int_member (object, "version", file->version);

    json_object_set_int_member (object, "size", file->file_size);

    block_id_array = json_array ();
    int i;
    for (i = 0; i < file->n_blocks; ++i) {
        json_array_append_new (block_id_array, json_string(file->blk_sha1s[i]));
    }
    json_object_set_new (object, "block_ids", block_id_array);

    char *data = json_dumps (object, JSON_SORT_KEYS);
    *len = strlen(data);

    unsigned char sha1[20];
    calculate_sha1 (sha1, data, *len);
    rawdata_to_hex (sha1, file->file_id, 20);

    json_decref (object);
    return (guint8 *)data;
}

static guint8 *
syncwerk_to_data (Syncwerk *file, int *len)
{
    if (file->version > 0) {
        guint8 *data;
        int orig_len;
        guint8 *compressed;

        data = syncwerk_to_json (file, &orig_len);
        if (!data)
            return NULL;

        if (syncw_compress (data, orig_len, &compressed, len) < 0) {
            syncw_warning ("Failed to compress file object %s.\n", file->file_id);
            g_free (data);
            return NULL;
        }
        g_free (data);
        return compressed;
    } else
        return syncwerk_to_v0_data (file, len);
}

int
syncwerk_save (SyncwFSManager *fs_mgr,
              const char *repo_id,
              int version,
              Syncwerk *file)
{
    guint8 *data;
    int len;
    int ret = 0;

    data = syncwerk_to_data (file, &len);
    if (!data)
        return -1;

    if (syncw_obj_store_write_obj (fs_mgr->obj_store, repo_id, version, file->file_id,
                                  data, len, FALSE) < 0)
        ret = -1;

    g_free (data);
    return ret;
}

static void compute_dir_id_v0 (SyncwDir *dir, GList *entries)
{
    SHA_CTX ctx;
    GList *p;
    uint8_t sha1[20];
    SyncwDirent *dent;
    guint32 mode_le;

    /* ID for empty dirs is EMPTY_SHA1. */
    if (entries == NULL) {
        memset (dir->dir_id, '0', 40);
        return;
    }

    SHA1_Init (&ctx);
    for (p = entries; p; p = p->next) {
        dent = (SyncwDirent *)p->data;
        SHA1_Update (&ctx, dent->id, 40);
        SHA1_Update (&ctx, dent->name, dent->name_len);
        /* Convert mode to little endian before compute. */
        if (G_BYTE_ORDER == G_BIG_ENDIAN)
            mode_le = GUINT32_SWAP_LE_BE (dent->mode);
        else
            mode_le = dent->mode;
        SHA1_Update (&ctx, &mode_le, sizeof(mode_le));
    }
    SHA1_Final (sha1, &ctx);

    rawdata_to_hex (sha1, dir->dir_id, 20);
}

SyncwDir *
syncw_dir_new (const char *id, GList *entries, int version)
{
    SyncwDir *dir;

    dir = g_new0(SyncwDir, 1);

    dir->version = version;
    if (id != NULL) {
        memcpy(dir->dir_id, id, 40);
        dir->dir_id[40] = '\0';
    } else if (version == 0) {
        compute_dir_id_v0 (dir, entries);
    }
    dir->entries = entries;

    if (dir->entries != NULL)
        dir->ondisk = syncw_dir_to_data (dir, &dir->ondisk_size);
    else
        memcpy (dir->dir_id, EMPTY_SHA1, 40);

    return dir;
}

void
syncw_dir_free (SyncwDir *dir)
{
    if (dir == NULL)
        return;

    GList *ptr = dir->entries;
    while (ptr) {
        syncw_dirent_free ((SyncwDirent *)ptr->data);
        ptr = ptr->next;
    }

    g_list_free (dir->entries);
    g_free (dir->ondisk);
    g_free(dir);
}

SyncwDirent *
syncw_dirent_new (int version, const char *sha1, int mode, const char *name,
                 gint64 mtime, const char *modifier, gint64 size)
{
    SyncwDirent *dent;

    dent = g_new0 (SyncwDirent, 1);
    dent->version = version;
    memcpy(dent->id, sha1, 40);
    dent->id[40] = '\0';
    /* Mode for files must have 0644 set. To prevent the caller from forgetting,
     * we set the bits here.
     */
    if (S_ISREG(mode))
        dent->mode = (mode | 0644);
    else
        dent->mode = mode;
    dent->name = g_strdup(name);
    dent->name_len = strlen(name);

    if (version > 0) {
        dent->mtime = mtime;
        if (S_ISREG(mode)) {
            dent->modifier = g_strdup(modifier);
            dent->size = size;
        }
    }

    return dent;
}

void 
syncw_dirent_free (SyncwDirent *dent)
{
    if (!dent)
        return;
    g_free (dent->name);
    g_free (dent->modifier);
    g_free (dent);
}

SyncwDirent *
syncw_dirent_dup (SyncwDirent *dent)
{
    SyncwDirent *new_dent;

    new_dent = g_memdup (dent, sizeof(SyncwDirent));
    new_dent->name = g_strdup(dent->name);
    new_dent->modifier = g_strdup(dent->modifier);

    return new_dent;
}

static SyncwDir *
syncw_dir_from_v0_data (const char *dir_id, const uint8_t *data, int len)
{
    SyncwDir *root;
    SyncwDirent *dent;
    const uint8_t *ptr;
    int remain;
    int dirent_base_size;
    guint32 meta_type;
    guint32 name_len;

    ptr = data;
    remain = len;

    meta_type = get32bit (&ptr);
    remain -= 4;
    if (meta_type != SYNCW_METADATA_TYPE_DIR) {
        syncw_warning ("Data does not contain a directory.\n");
        return NULL;
    }

    root = g_new0(SyncwDir, 1);
    root->object.type = SYNCW_METADATA_TYPE_DIR;
    root->version = 0;
    memcpy(root->dir_id, dir_id, 40);
    root->dir_id[40] = '\0';

    dirent_base_size = 2 * sizeof(guint32) + 40;
    while (remain > dirent_base_size) {
        dent = g_new0(SyncwDirent, 1);

        dent->version = 0;
        dent->mode = get32bit (&ptr);
        memcpy (dent->id, ptr, 40);
        dent->id[40] = '\0';
        ptr += 40;
        name_len = get32bit (&ptr);
        remain -= dirent_base_size;
        if (remain >= name_len) {
            dent->name_len = MIN (name_len, SYNCW_DIR_NAME_LEN - 1);
            dent->name = g_strndup((const char *)ptr, dent->name_len);
            ptr += dent->name_len;
            remain -= dent->name_len;
        } else {
            syncw_warning ("Bad data format for dir objcet %s.\n", dir_id);
            g_free (dent);
            goto bad;
        }

        root->entries = g_list_prepend (root->entries, dent);
    }

    root->entries = g_list_reverse (root->entries);

    return root;

bad:
    syncw_dir_free (root);
    return NULL;
}

static SyncwDirent *
parse_dirent (const char *dir_id, int version, json_t *object)
{
    guint32 mode;
    const char *id;
    const char *name;
    gint64 mtime;
    const char *modifier;
    gint64 size;

    mode = (guint32) json_object_get_int_member (object, "mode");

    id = json_object_get_string_member (object, "id");
    if (!id) {
        syncw_debug ("Dirent id not set for dir object %s.\n", dir_id);
        return NULL;
    }
    if (!is_object_id_valid (id)) {
        syncw_debug ("Dirent id is invalid for dir object %s.\n", dir_id);
        return NULL;
    }

    name = json_object_get_string_member (object, "name");
    if (!name) {
        syncw_debug ("Dirent name not set for dir object %s.\n", dir_id);
        return NULL;
    }

    mtime = json_object_get_int_member (object, "mtime");
    if (S_ISREG(mode)) {
        modifier = json_object_get_string_member (object, "modifier");
        if (!modifier) {
            syncw_debug ("Dirent modifier not set for dir object %s.\n", dir_id);
            return NULL;
        }
        size = json_object_get_int_member (object, "size");
    }

    SyncwDirent *dirent = g_new0 (SyncwDirent, 1);
    dirent->version = version;
    dirent->mode = mode;
    memcpy (dirent->id, id, 40);
    dirent->name_len = strlen(name);
    dirent->name = g_strdup(name);
    dirent->mtime = mtime;
    if (S_ISREG(mode)) {
        dirent->modifier = g_strdup(modifier);
        dirent->size = size;
    }

    return dirent;
}

static SyncwDir *
syncw_dir_from_json_object (const char *dir_id, json_t *object)
{
    json_t *dirent_array = NULL;
    int type;
    int version;
    SyncwDir *dir = NULL;

    /* Sanity checks. */
    type = json_object_get_int_member (object, "type");
    if (type != SYNCW_METADATA_TYPE_DIR) {
        syncw_debug ("Object %s is not a dir.\n", dir_id);
        return NULL;
    }

    version = (int) json_object_get_int_member (object, "version");
    if (version < 1) {
        syncw_debug ("Dir object %s version should be > 0, version is %d.\n",
                    dir_id, version);
        return NULL;
    }

    dirent_array = json_object_get (object, "dirents");
    if (!dirent_array) {
        syncw_debug ("No dirents in dir object %s.\n", dir_id);
        return NULL;
    }

    dir = g_new0 (SyncwDir, 1);

    dir->object.type = SYNCW_METADATA_TYPE_DIR;

    memcpy (dir->dir_id, dir_id, 40);
    dir->version = version;

    size_t n_dirents = json_array_size (dirent_array);
    int i;
    json_t *dirent_obj;
    SyncwDirent *dirent;
    for (i = 0; i < n_dirents; ++i) {
        dirent_obj = json_array_get (dirent_array, i);
        dirent = parse_dirent (dir_id, version, dirent_obj);
        if (!dirent) {
            syncw_dir_free (dir);
            return NULL;
        }
        dir->entries = g_list_prepend (dir->entries, dirent);
    }
    dir->entries = g_list_reverse (dir->entries);

    return dir;
}

static SyncwDir *
syncw_dir_from_json (const char *dir_id, uint8_t *data, int len)
{
    guint8 *decompressed;
    int outlen;
    json_t *object = NULL;
    json_error_t error;
    SyncwDir *dir;

    if (syncw_decompress (data, len, &decompressed, &outlen) < 0) {
        syncw_warning ("Failed to decompress dir object %s.\n", dir_id);
        return NULL;
    }

    object = json_loadb ((const char *)decompressed, outlen, 0, &error);
    g_free (decompressed);
    if (!object) {
        if (error.text)
            syncw_warning ("Failed to load syncwdir json object: %s.\n", error.text);
        else
            syncw_warning ("Failed to load syncwdir json object.\n");
        return NULL;
    }

    dir = syncw_dir_from_json_object (dir_id, object);

    json_decref (object);
    return dir;
}

SyncwDir *
syncw_dir_from_data (const char *dir_id, uint8_t *data, int len,
                    gboolean is_json)
{
    if (is_json)
        return syncw_dir_from_json (dir_id, data, len);
    else
        return syncw_dir_from_v0_data (dir_id, data, len);
}

inline static int
ondisk_dirent_size (SyncwDirent *dirent)
{
    return sizeof(DirentOndisk) + dirent->name_len;
}

static void *
syncw_dir_to_v0_data (SyncwDir *dir, int *len)
{
    SyncwdirOndisk *ondisk;
    int dir_ondisk_size = sizeof(SyncwdirOndisk);
    GList *dirents = dir->entries;
    GList *ptr;
    SyncwDirent *de;
    char *p;
    DirentOndisk *de_ondisk;

    for (ptr = dirents; ptr; ptr = ptr->next) {
        de = ptr->data;
        dir_ondisk_size += ondisk_dirent_size (de);
    }

    *len = dir_ondisk_size;
    ondisk = (SyncwdirOndisk *) g_new0 (char, dir_ondisk_size);

    ondisk->type = htonl (SYNCW_METADATA_TYPE_DIR);
    p = ondisk->dirents;
    for (ptr = dirents; ptr; ptr = ptr->next) {
        de = ptr->data;
        de_ondisk = (DirentOndisk *) p;

        de_ondisk->mode = htonl(de->mode);
        memcpy (de_ondisk->id, de->id, 40);
        de_ondisk->name_len = htonl (de->name_len);
        memcpy (de_ondisk->name, de->name, de->name_len);

        p += ondisk_dirent_size (de);
    }

    return (void *)ondisk;
}

static void
add_to_dirent_array (json_t *array, SyncwDirent *dirent)
{
    json_t *object;

    object = json_object ();
    json_object_set_int_member (object, "mode", dirent->mode);
    json_object_set_string_member (object, "id", dirent->id);
    json_object_set_string_member (object, "name", dirent->name);
    json_object_set_int_member (object, "mtime", dirent->mtime);
    if (S_ISREG(dirent->mode)) {
        json_object_set_string_member (object, "modifier", dirent->modifier);
        json_object_set_int_member (object, "size", dirent->size);
    }

    json_array_append_new (array, object);
}

static void *
syncw_dir_to_json (SyncwDir *dir, int *len)
{
    json_t *object, *dirent_array;
    GList *ptr;
    SyncwDirent *dirent;

    object = json_object ();

    json_object_set_int_member (object, "type", SYNCW_METADATA_TYPE_DIR);
    json_object_set_int_member (object, "version", dir->version);

    dirent_array = json_array ();
    for (ptr = dir->entries; ptr; ptr = ptr->next) {
        dirent = ptr->data;
        add_to_dirent_array (dirent_array, dirent);
    }
    json_object_set_new (object, "dirents", dirent_array);

    char *data = json_dumps (object, JSON_SORT_KEYS);
    *len = strlen(data);

    /* The dir object id is sha1 hash of the json object. */
    unsigned char sha1[20];
    calculate_sha1 (sha1, data, *len);
    rawdata_to_hex (sha1, dir->dir_id, 20);

    json_decref (object);
    return data;
}

void *
syncw_dir_to_data (SyncwDir *dir, int *len)
{
    if (dir->version > 0) {
        guint8 *data;
        int orig_len;
        guint8 *compressed;

        data = syncw_dir_to_json (dir, &orig_len);
        if (!data)
            return NULL;

        if (syncw_compress (data, orig_len, &compressed, len) < 0) {
            syncw_warning ("Failed to compress dir object %s.\n", dir->dir_id);
            g_free (data);
            return NULL;
        }

        g_free (data);
        return compressed;
    } else
        return syncw_dir_to_v0_data (dir, len);
}

int
syncw_dir_save (SyncwFSManager *fs_mgr,
               const char *repo_id,
               int version,
               SyncwDir *dir)
{
    int ret = 0;

    /* Don't need to save empty dir on disk. */
    if (memcmp (dir->dir_id, EMPTY_SHA1, 40) == 0)
        return 0;

    if (syncw_obj_store_write_obj (fs_mgr->obj_store, repo_id, version, dir->dir_id,
                                  dir->ondisk, dir->ondisk_size, FALSE) < 0)
        ret = -1;

    return ret;
}

SyncwDir *
syncw_fs_manager_get_syncwdir (SyncwFSManager *mgr,
                             const char *repo_id,
                             int version,
                             const char *dir_id)
{
    void *data;
    int len;
    SyncwDir *dir;

    /* TODO: add hash cache */

    if (memcmp (dir_id, EMPTY_SHA1, 40) == 0) {
        dir = g_new0 (SyncwDir, 1);
        dir->version = version;
        memset (dir->dir_id, '0', 40);
        return dir;
    }

    if (syncw_obj_store_read_obj (mgr->obj_store, repo_id, version,
                                 dir_id, &data, &len) < 0) {
        syncw_warning ("[fs mgr] Failed to read dir %s.\n", dir_id);
        return NULL;
    }

    dir = syncw_dir_from_data (dir_id, data, len, (version > 0));
    g_free (data);

    return dir;
}

static gint
compare_dirents (gconstpointer a, gconstpointer b)
{
    const SyncwDirent *denta = a, *dentb = b;

    return strcmp (dentb->name, denta->name);
}

static gboolean
is_dirents_sorted (GList *dirents)
{
    GList *ptr;
    SyncwDirent *dent, *dent_n;
    gboolean ret = TRUE;

    for (ptr = dirents; ptr != NULL; ptr = ptr->next) {
        dent = ptr->data;
        if (!ptr->next)
            break;
        dent_n = ptr->next->data;

        /* If dirents are not sorted in descending order, return FALSE. */
        if (strcmp (dent->name, dent_n->name) < 0) {
            ret = FALSE;
            break;
        }
    }

    return ret;
}

SyncwDir *
syncw_fs_manager_get_syncwdir_sorted (SyncwFSManager *mgr,
                                    const char *repo_id,
                                    int version,
                                    const char *dir_id)
{
    SyncwDir *dir = syncw_fs_manager_get_syncwdir(mgr, repo_id, version, dir_id);

    if (!dir)
        return NULL;

    /* Only some very old dir objects are not sorted. */
    if (version > 0)
        return dir;

    if (!is_dirents_sorted (dir->entries))
        dir->entries = g_list_sort (dir->entries, compare_dirents);

    return dir;
}

SyncwDir *
syncw_fs_manager_get_syncwdir_sorted_by_path (SyncwFSManager *mgr,
                                            const char *repo_id,
                                            int version,
                                            const char *root_id,
                                            const char *path)
{
    SyncwDir *dir = syncw_fs_manager_get_syncwdir_by_path (mgr, repo_id,
                                                        version, root_id,
                                                        path, NULL);

    if (!dir)
        return NULL;

    /* Only some very old dir objects are not sorted. */
    if (version > 0)
        return dir;

    if (!is_dirents_sorted (dir->entries))
        dir->entries = g_list_sort (dir->entries, compare_dirents);

    return dir;
}

static int
parse_metadata_type_v0 (const uint8_t *data, int len)
{
    const uint8_t *ptr = data;

    if (len < sizeof(guint32))
        return SYNCW_METADATA_TYPE_INVALID;

    return (int)(get32bit(&ptr));
}

static int
parse_metadata_type_json (const char *obj_id, uint8_t *data, int len)
{
    guint8 *decompressed;
    int outlen;
    json_t *object;
    json_error_t error;
    int type;

    if (syncw_decompress (data, len, &decompressed, &outlen) < 0) {
        syncw_warning ("Failed to decompress fs object %s.\n", obj_id);
        return SYNCW_METADATA_TYPE_INVALID;
    }

    object = json_loadb ((const char *)decompressed, outlen, 0, &error);
    g_free (decompressed);
    if (!object) {
        if (error.text)
            syncw_warning ("Failed to load fs json object: %s.\n", error.text);
        else
            syncw_warning ("Failed to load fs json object.\n");
        return SYNCW_METADATA_TYPE_INVALID;
    }

    type = json_object_get_int_member (object, "type");

    json_decref (object);
    return type;
}

int
syncw_metadata_type_from_data (const char *obj_id,
                              uint8_t *data, int len, gboolean is_json)
{
    if (is_json)
        return parse_metadata_type_json (obj_id, data, len);
    else
        return parse_metadata_type_v0 (data, len);
}

SyncwFSObject *
fs_object_from_v0_data (const char *obj_id, const uint8_t *data, int len)
{
    int type = parse_metadata_type_v0 (data, len);

    if (type == SYNCW_METADATA_TYPE_FILE)
        return (SyncwFSObject *)syncwerk_from_v0_data (obj_id, data, len);
    else if (type == SYNCW_METADATA_TYPE_DIR)
        return (SyncwFSObject *)syncw_dir_from_v0_data (obj_id, data, len);
    else {
        syncw_warning ("Invalid object type %d.\n", type);
        return NULL;
    }
}

SyncwFSObject *
fs_object_from_json (const char *obj_id, uint8_t *data, int len)
{
    guint8 *decompressed;
    int outlen;
    json_t *object;
    json_error_t error;
    int type;
    SyncwFSObject *fs_obj;

    if (syncw_decompress (data, len, &decompressed, &outlen) < 0) {
        syncw_warning ("Failed to decompress fs object %s.\n", obj_id);
        return NULL;
    }

    object = json_loadb ((const char *)decompressed, outlen, 0, &error);
    g_free (decompressed);
    if (!object) {
        if (error.text)
            syncw_warning ("Failed to load fs json object: %s.\n", error.text);
        else
            syncw_warning ("Failed to load fs json object.\n");
        return NULL;
    }

    type = json_object_get_int_member (object, "type");

    if (type == SYNCW_METADATA_TYPE_FILE)
        fs_obj = (SyncwFSObject *)syncwerk_from_json_object (obj_id, object);
    else if (type == SYNCW_METADATA_TYPE_DIR)
        fs_obj = (SyncwFSObject *)syncw_dir_from_json_object (obj_id, object);
    else {
        syncw_warning ("Invalid fs type %d.\n", type);
        json_decref (object);
        return NULL;
    }

    json_decref (object);

    return fs_obj;
}

SyncwFSObject *
syncw_fs_object_from_data (const char *obj_id,
                          uint8_t *data, int len,
                          gboolean is_json)
{
    if (is_json)
        return fs_object_from_json (obj_id, data, len);
    else
        return fs_object_from_v0_data (obj_id, data, len);
}

void
syncw_fs_object_free (SyncwFSObject *obj)
{
    if (!obj)
        return;

    if (obj->type == SYNCW_METADATA_TYPE_FILE)
        syncwerk_unref ((Syncwerk *)obj);
    else if (obj->type == SYNCW_METADATA_TYPE_DIR)
        syncw_dir_free ((SyncwDir *)obj);
}

BlockList *
block_list_new ()
{
    BlockList *bl = g_new0 (BlockList, 1);

    bl->block_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    bl->block_ids = g_ptr_array_new_with_free_func (g_free);

    return bl;
}

void
block_list_free (BlockList *bl)
{
    if (bl->block_hash)
        g_hash_table_destroy (bl->block_hash);
    g_ptr_array_free (bl->block_ids, TRUE);
    g_free (bl);
}

void
block_list_insert (BlockList *bl, const char *block_id)
{
    if (g_hash_table_lookup (bl->block_hash, block_id))
        return;

    char *key = g_strdup(block_id);
    g_hash_table_replace (bl->block_hash, key, key);
    g_ptr_array_add (bl->block_ids, g_strdup(block_id));
    ++bl->n_blocks;
}

BlockList *
block_list_difference (BlockList *bl1, BlockList *bl2)
{
    BlockList *bl;
    int i;
    char *block_id;
    char *key;

    bl = block_list_new ();

    for (i = 0; i < bl1->block_ids->len; ++i) {
        block_id = g_ptr_array_index (bl1->block_ids, i);
        if (g_hash_table_lookup (bl2->block_hash, block_id) == NULL) {
            key = g_strdup(block_id);
            g_hash_table_replace (bl->block_hash, key, key);
            g_ptr_array_add (bl->block_ids, g_strdup(block_id));
            ++bl->n_blocks;
        }
    }

    return bl;
}

static int
traverse_file (SyncwFSManager *mgr,
               const char *repo_id,
               int version,
               const char *id,
               TraverseFSTreeCallback callback,
               void *user_data,
               gboolean skip_errors)
{
    gboolean stop = FALSE;

    if (memcmp (id, EMPTY_SHA1, 40) == 0)
        return 0;

    if (!callback (mgr, repo_id, version, id, SYNCW_METADATA_TYPE_FILE, user_data, &stop) &&
        !skip_errors)
        return -1;

    return 0;
}

static int
traverse_dir (SyncwFSManager *mgr,
              const char *repo_id,
              int version,
              const char *id,
              TraverseFSTreeCallback callback,
              void *user_data,
              gboolean skip_errors)
{
    SyncwDir *dir;
    GList *p;
    SyncwDirent *syncw_dent;
    gboolean stop = FALSE;

    if (!callback (mgr, repo_id, version,
                   id, SYNCW_METADATA_TYPE_DIR, user_data, &stop) &&
        !skip_errors)
        return -1;

    if (stop)
        return 0;

    dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, id);
    if (!dir) {
        syncw_warning ("[fs-mgr]get syncwdir %s failed\n", id);
        if (skip_errors)
            return 0;
        return -1;
    }
    for (p = dir->entries; p; p = p->next) {
        syncw_dent = (SyncwDirent *)p->data;

        if (S_ISREG(syncw_dent->mode)) {
            if (traverse_file (mgr, repo_id, version, syncw_dent->id,
                               callback, user_data, skip_errors) < 0) {
                if (!skip_errors) {
                    syncw_dir_free (dir);
                    return -1;
                }
            }
        } else if (S_ISDIR(syncw_dent->mode)) {
            if (traverse_dir (mgr, repo_id, version, syncw_dent->id,
                              callback, user_data, skip_errors) < 0) {
                if (!skip_errors) {
                    syncw_dir_free (dir);
                    return -1;
                }
            }
        }
    }

    syncw_dir_free (dir);
    return 0;
}

int
syncw_fs_manager_traverse_tree (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *root_id,
                               TraverseFSTreeCallback callback,
                               void *user_data,
                               gboolean skip_errors)
{
    if (strcmp (root_id, EMPTY_SHA1) == 0) {
        return 0;
    }
    return traverse_dir (mgr, repo_id, version, root_id, callback, user_data, skip_errors);
}

static int
traverse_dir_path (SyncwFSManager *mgr,
                   const char *repo_id,
                   int version,
                   const char *dir_path,
                   SyncwDirent *dent,
                   TraverseFSPathCallback callback,
                   void *user_data)
{
    SyncwDir *dir;
    GList *p;
    SyncwDirent *syncw_dent;
    gboolean stop = FALSE;
    char *sub_path;
    int ret = 0;

    if (!callback (mgr, dir_path, dent, user_data, &stop))
        return -1;

    if (stop)
        return 0;

    dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, dent->id);
    if (!dir) {
        syncw_warning ("get syncwdir %s:%s failed\n", repo_id, dent->id);
        return -1;
    }

    for (p = dir->entries; p; p = p->next) {
        syncw_dent = (SyncwDirent *)p->data;
        sub_path = g_strconcat (dir_path, "/", syncw_dent->name, NULL);

        if (S_ISREG(syncw_dent->mode)) {
            if (!callback (mgr, sub_path, syncw_dent, user_data, &stop)) {
                g_free (sub_path);
                ret = -1;
                break;
            }
        } else if (S_ISDIR(syncw_dent->mode)) {
            if (traverse_dir_path (mgr, repo_id, version, sub_path, syncw_dent,
                                   callback, user_data) < 0) {
                g_free (sub_path);
                ret = -1;
                break;
            }
        }
        g_free (sub_path);
    }

    syncw_dir_free (dir);
    return ret;
}

int
syncw_fs_manager_traverse_path (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *root_id,
                               const char *dir_path,
                               TraverseFSPathCallback callback,
                               void *user_data)
{
    SyncwDirent *dent;
    int ret = 0;

    dent = syncw_fs_manager_get_dirent_by_path (mgr, repo_id, version,
                                               root_id, dir_path, NULL);
    if (!dent) {
        syncw_warning ("Failed to get dirent for %.8s:%s.\n", repo_id, dir_path);
        return -1;
    }

    ret = traverse_dir_path (mgr, repo_id, version, dir_path, dent,
                             callback, user_data);

    syncw_dirent_free (dent);
    return ret;
}

static gboolean
fill_blocklist (SyncwFSManager *mgr,
                const char *repo_id, int version,
                const char *obj_id, int type,
                void *user_data, gboolean *stop)
{
    BlockList *bl = user_data;
    Syncwerk *syncwerk;
    int i;

    if (type == SYNCW_METADATA_TYPE_FILE) {
        syncwerk = syncw_fs_manager_get_syncwerk (mgr, repo_id, version, obj_id);
        if (!syncwerk) {
            syncw_warning ("[fs mgr] Failed to find file %s.\n", obj_id);
            return FALSE;
        }

        for (i = 0; i < syncwerk->n_blocks; ++i)
            block_list_insert (bl, syncwerk->blk_sha1s[i]);

        syncwerk_unref (syncwerk);
    }

    return TRUE;
}

int
syncw_fs_manager_populate_blocklist (SyncwFSManager *mgr,
                                    const char *repo_id,
                                    int version,
                                    const char *root_id,
                                    BlockList *bl)
{
    return syncw_fs_manager_traverse_tree (mgr, repo_id, version, root_id,
                                          fill_blocklist,
                                          bl, FALSE);
}

gboolean
syncw_fs_manager_object_exists (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *id)
{
    /* Empty file and dir always exists. */
    if (memcmp (id, EMPTY_SHA1, 40) == 0)
        return TRUE;

    return syncw_obj_store_obj_exists (mgr->obj_store, repo_id, version, id);
}

void
syncw_fs_manager_delete_object (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *id)
{
    syncw_obj_store_delete_obj (mgr->obj_store, repo_id, version, id);
}

gint64
syncw_fs_manager_get_file_size (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *file_id)
{
    Syncwerk *file;
    gint64 file_size;

    file = syncw_fs_manager_get_syncwerk (syncw->fs_mgr, repo_id, version, file_id);
    if (!file) {
        syncw_warning ("Couldn't get file %s:%s\n", repo_id, file_id);
        return -1;
    }

    file_size = file->file_size;

    syncwerk_unref (file);
    return file_size;
}

static gint64
get_dir_size (SyncwFSManager *mgr, const char *repo_id, int version, const char *id)
{
    SyncwDir *dir;
    SyncwDirent *syncw_dent;
    guint64 size = 0;
    gint64 result;
    GList *p;

    dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, id);
    if (!dir)
        return -1;

    for (p = dir->entries; p; p = p->next) {
        syncw_dent = (SyncwDirent *)p->data;

        if (S_ISREG(syncw_dent->mode)) {
            if (dir->version > 0)
                result = syncw_dent->size;
            else {
                result = syncw_fs_manager_get_file_size (mgr,
                                                        repo_id,
                                                        version,
                                                        syncw_dent->id);
                if (result < 0) {
                    syncw_dir_free (dir);
                    return result;
                }
            }
            size += result;
        } else if (S_ISDIR(syncw_dent->mode)) {
            result = get_dir_size (mgr, repo_id, version, syncw_dent->id);
            if (result < 0) {
                syncw_dir_free (dir);
                return result;
            }
            size += result;
        }
    }

    syncw_dir_free (dir);
    return size;
}

gint64
syncw_fs_manager_get_fs_size (SyncwFSManager *mgr,
                             const char *repo_id,
                             int version,
                             const char *root_id)
{
     if (strcmp (root_id, EMPTY_SHA1) == 0)
        return 0;
     return get_dir_size (mgr, repo_id, version, root_id);
}

static int
count_dir_files (SyncwFSManager *mgr, const char *repo_id, int version, const char *id)
{
    SyncwDir *dir;
    SyncwDirent *syncw_dent;
    int count = 0;
    int result;
    GList *p;

    dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, id);
    if (!dir)
        return -1;

    for (p = dir->entries; p; p = p->next) {
        syncw_dent = (SyncwDirent *)p->data;

        if (S_ISREG(syncw_dent->mode)) {
            count ++;
        } else if (S_ISDIR(syncw_dent->mode)) {
            result = count_dir_files (mgr, repo_id, version, syncw_dent->id);
            if (result < 0) {
                syncw_dir_free (dir);
                return result;
            }
            count += result;
        }
    }

    syncw_dir_free (dir);
    return count;
}

static int
get_file_count_info (SyncwFSManager *mgr,
                     const char *repo_id,
                     int version,
                     const char *id,
                     gint64 *dir_count,
                     gint64 *file_count,
                     gint64 *size)
{
    SyncwDir *dir;
    SyncwDirent *syncw_dent;
    GList *p;
    int ret = 0;

    dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, id);
    if (!dir)
        return -1;

    for (p = dir->entries; p; p = p->next) {
        syncw_dent = (SyncwDirent *)p->data;

        if (S_ISREG(syncw_dent->mode)) {
            (*file_count)++;
            if (version > 0)
                (*size) += syncw_dent->size;
        } else if (S_ISDIR(syncw_dent->mode)) {
            (*dir_count)++;
            ret = get_file_count_info (mgr, repo_id, version, syncw_dent->id,
                                       dir_count, file_count, size);
        }
    }
    syncw_dir_free (dir);

    return ret;
}

int
syncw_fs_manager_count_fs_files (SyncwFSManager *mgr,
                                const char *repo_id,
                                int version,
                                const char *root_id)
{
     if (strcmp (root_id, EMPTY_SHA1) == 0)
        return 0;
     return count_dir_files (mgr, repo_id, version, root_id);
}

SyncwDir *
syncw_fs_manager_get_syncwdir_by_path (SyncwFSManager *mgr,
                                     const char *repo_id,
                                     int version,
                                     const char *root_id,
                                     const char *path,
                                     GError **error)
{
    SyncwDir *dir;
    SyncwDirent *dent;
    const char *dir_id = root_id;
    char *name, *saveptr;
    char *tmp_path = g_strdup(path);

    dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, dir_id);
    if (!dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_DIR_MISSING, "directory is missing");
        g_free (tmp_path);
        return NULL;
    }

    name = strtok_r (tmp_path, "/", &saveptr);
    while (name != NULL) {
        GList *l;
        for (l = dir->entries; l != NULL; l = l->next) {
            dent = l->data;

            if (strcmp(dent->name, name) == 0 && S_ISDIR(dent->mode)) {
                dir_id = dent->id;
                break;
            }
        }

        if (!l) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_PATH_NO_EXIST,
                         "Path does not exists %s", path);
            syncw_dir_free (dir);
            dir = NULL;
            break;
        }

        SyncwDir *prev = dir;
        dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, dir_id);
        syncw_dir_free (prev);

        if (!dir) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_DIR_MISSING,
                         "directory is missing");
            break;
        }

        name = strtok_r (NULL, "/", &saveptr);
    }

    g_free (tmp_path);
    return dir;
}

char *
syncw_fs_manager_path_to_obj_id (SyncwFSManager *mgr,
                                const char *repo_id,
                                int version,
                                const char *root_id,
                                const char *path,
                                guint32 *mode,
                                GError **error)
{
    char *copy = g_strdup (path);
    int off = strlen(copy) - 1;
    char *slash, *name;
    SyncwDir *base_dir = NULL;
    SyncwDirent *dent;
    GList *p;
    char *obj_id = NULL;

    while (off >= 0 && copy[off] == '/')
        copy[off--] = 0;

    if (strlen(copy) == 0) {
        /* the path is root "/" */
        if (mode) {
            *mode = S_IFDIR;
        }
        obj_id = g_strdup(root_id);
        goto out;
    }

    slash = strrchr (copy, '/');
    if (!slash) {
        base_dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, root_id);
        if (!base_dir) {
            syncw_warning ("Failed to find root dir %s.\n", root_id);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, " ");
            goto out;
        }
        name = copy;
    } else {
        *slash = 0;
        name = slash + 1;
        GError *tmp_error = NULL;
        base_dir = syncw_fs_manager_get_syncwdir_by_path (mgr,
                                                        repo_id,
                                                        version,
                                                        root_id,
                                                        copy,
                                                        &tmp_error);
        if (tmp_error &&
            !g_error_matches(tmp_error,
                             SYNCWERK_DOMAIN,
                             SYNCW_ERR_PATH_NO_EXIST)) {
            syncw_warning ("Failed to get dir for %s.\n", copy);
            g_propagate_error (error, tmp_error);
            goto out;
        }

        /* The path doesn't exist in this commit. */
        if (!base_dir) {
            g_propagate_error (error, tmp_error);
            goto out;
        }
    }

    for (p = base_dir->entries; p != NULL; p = p->next) {
        dent = p->data;

        if (!is_object_id_valid (dent->id))
            continue;

        if (strcmp (dent->name, name) == 0) {
            obj_id = g_strdup (dent->id);
            if (mode) {
                *mode = dent->mode;
            }
            break;
        }
    }

out:
    if (base_dir)
        syncw_dir_free (base_dir);
    g_free (copy);
    return obj_id;
}

char *
syncw_fs_manager_get_syncwerk_id_by_path (SyncwFSManager *mgr,
                                        const char *repo_id,
                                        int version,
                                        const char *root_id,
                                        const char *path,
                                        GError **error)
{
    guint32 mode;
    char *file_id;

    file_id = syncw_fs_manager_path_to_obj_id (mgr, repo_id, version,
                                              root_id, path, &mode, error);

    if (!file_id)
        return NULL;

    if (file_id && S_ISDIR(mode)) {
        g_free (file_id);
        return NULL;
    }

    return file_id;
}

char *
syncw_fs_manager_get_syncwdir_id_by_path (SyncwFSManager *mgr,
                                        const char *repo_id,
                                        int version,
                                        const char *root_id,
                                        const char *path,
                                        GError **error)
{
    guint32 mode = 0;
    char *dir_id;

    dir_id = syncw_fs_manager_path_to_obj_id (mgr, repo_id, version,
                                             root_id, path, &mode, error);

    if (!dir_id)
        return NULL;

    if (dir_id && !S_ISDIR(mode)) {
        g_free (dir_id);
        return NULL;
    }

    return dir_id;
}

SyncwDirent *
syncw_fs_manager_get_dirent_by_path (SyncwFSManager *mgr,
                                    const char *repo_id,
                                    int version,
                                    const char *root_id,
                                    const char *path,
                                    GError **error)
{
    SyncwDirent *dent = NULL;
    SyncwDir *dir = NULL;
    char *parent_dir = NULL;
    char *file_name = NULL;

    parent_dir  = g_path_get_dirname(path);
    file_name = g_path_get_basename(path);

    if (strcmp (parent_dir, ".") == 0) {
        dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, root_id);
        if (!dir) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_DIR_MISSING, "directory is missing");
        }
    } else
        dir = syncw_fs_manager_get_syncwdir_by_path (mgr, repo_id, version,
                                                   root_id, parent_dir, error);

    if (!dir) {
        syncw_warning ("dir %s doesn't exist in repo %.8s.\n", parent_dir, repo_id);
        goto out;
    }

    GList *p;
    for (p = dir->entries; p; p = p->next) {
        SyncwDirent *d = p->data;
        if (strcmp (d->name, file_name) == 0) {
            dent = syncw_dirent_dup(d);
            break;
        }
    }

out:
    if (dir)
        syncw_dir_free (dir);
    g_free (parent_dir);
    g_free (file_name);

    return dent;
}

static gboolean
verify_syncwdir_v0 (const char *dir_id, const uint8_t *data, int len,
                   gboolean verify_id)
{
    guint32 meta_type;
    guint32 mode;
    char id[41];
    guint32 name_len;
    char name[SYNCW_DIR_NAME_LEN];
    const uint8_t *ptr;
    int remain;
    int dirent_base_size;
    SHA_CTX ctx;
    uint8_t sha1[20];
    char check_id[41];

    if (len < sizeof(SyncwdirOndisk)) {
        syncw_warning ("[fs mgr] Corrupt syncwdir object %s.\n", dir_id);
        return FALSE;
    }

    ptr = data;
    remain = len;

    meta_type = get32bit (&ptr);
    remain -= 4;
    if (meta_type != SYNCW_METADATA_TYPE_DIR) {
        syncw_warning ("Data does not contain a directory.\n");
        return FALSE;
    }

    if (verify_id)
        SHA1_Init (&ctx);

    dirent_base_size = 2 * sizeof(guint32) + 40;
    while (remain > dirent_base_size) {
        mode = get32bit (&ptr);
        memcpy (id, ptr, 40);
        id[40] = '\0';
        ptr += 40;
        name_len = get32bit (&ptr);
        remain -= dirent_base_size;
        if (remain >= name_len) {
            name_len = MIN (name_len, SYNCW_DIR_NAME_LEN - 1);
            memcpy (name, ptr, name_len);
            ptr += name_len;
            remain -= name_len;
        } else {
            syncw_warning ("Bad data format for dir objcet %s.\n", dir_id);
            return FALSE;
        }

        if (verify_id) {
            /* Convert mode to little endian before compute. */
            if (G_BYTE_ORDER == G_BIG_ENDIAN)
                mode = GUINT32_SWAP_LE_BE (mode);

            SHA1_Update (&ctx, id, 40);
            SHA1_Update (&ctx, name, name_len);
            SHA1_Update (&ctx, &mode, sizeof(mode));
        }
    }

    if (!verify_id)
        return TRUE;

    SHA1_Final (sha1, &ctx);
    rawdata_to_hex (sha1, check_id, 20);

    if (strcmp (check_id, dir_id) == 0)
        return TRUE;
    else
        return FALSE;
}

static gboolean
verify_fs_object_json (const char *obj_id, uint8_t *data, int len)
{
    guint8 *decompressed;
    int outlen;
    unsigned char sha1[20];
    char hex[41];

    if (syncw_decompress (data, len, &decompressed, &outlen) < 0) {
        syncw_warning ("Failed to decompress fs object %s.\n", obj_id);
        return FALSE;
    }

    calculate_sha1 (sha1, (const char *)decompressed, outlen);
    rawdata_to_hex (sha1, hex, 20);

    g_free (decompressed);
    return (strcmp(hex, obj_id) == 0);
}

static gboolean
verify_syncwdir (const char *dir_id, uint8_t *data, int len,
                gboolean verify_id, gboolean is_json)
{
    if (is_json)
        return verify_fs_object_json (dir_id, data, len);
    else
        return verify_syncwdir_v0 (dir_id, data, len, verify_id);
}
                                        
gboolean
syncw_fs_manager_verify_syncwdir (SyncwFSManager *mgr,
                                const char *repo_id,
                                int version,
                                const char *dir_id,
                                gboolean verify_id,
                                gboolean *io_error)
{
    void *data;
    int len;

    if (memcmp (dir_id, EMPTY_SHA1, 40) == 0) {
        return TRUE;
    }

    if (syncw_obj_store_read_obj (mgr->obj_store, repo_id, version,
                                 dir_id, &data, &len) < 0) {
        syncw_warning ("[fs mgr] Failed to read dir %s:%s.\n", repo_id, dir_id);
        *io_error = TRUE;
        return FALSE;
    }

    gboolean ret = verify_syncwdir (dir_id, data, len, verify_id, (version > 0));
    g_free (data);

    return ret;
}

static gboolean
verify_syncwerk_v0 (const char *id, const void *data, int len, gboolean verify_id)
{
    const SyncwerkOndisk *ondisk = data;
    SHA_CTX ctx;
    uint8_t sha1[20];
    char check_id[41];

    if (len < sizeof(SyncwerkOndisk)) {
        syncw_warning ("[fs mgr] Corrupt syncwerk object %s.\n", id);
        return FALSE;
    }

    if (ntohl(ondisk->type) != SYNCW_METADATA_TYPE_FILE) {
        syncw_warning ("[fd mgr] %s is not a file.\n", id);
        return FALSE;
    }

    int id_list_length = len - sizeof(SyncwerkOndisk);
    if (id_list_length % 20 != 0) {
        syncw_warning ("[fs mgr] Bad syncwerk id list length %d.\n", id_list_length);
        return FALSE;
    }

    if (!verify_id)
        return TRUE;

    SHA1_Init (&ctx);
    SHA1_Update (&ctx, ondisk->block_ids, len - sizeof(SyncwerkOndisk));
    SHA1_Final (sha1, &ctx);

    rawdata_to_hex (sha1, check_id, 20);

    if (strcmp (check_id, id) == 0)
        return TRUE;
    else
        return FALSE;
}

static gboolean
verify_syncwerk (const char *id, void *data, int len,
                gboolean verify_id, gboolean is_json)
{
    if (is_json)
        return verify_fs_object_json (id, data, len);
    else
        return verify_syncwerk_v0 (id, data, len, verify_id);
}

gboolean
syncw_fs_manager_verify_syncwerk (SyncwFSManager *mgr,
                                const char *repo_id,
                                int version,
                                const char *file_id,
                                gboolean verify_id,
                                gboolean *io_error)
{
    void *data;
    int len;

    if (memcmp (file_id, EMPTY_SHA1, 40) == 0) {
        return TRUE;
    }

    if (syncw_obj_store_read_obj (mgr->obj_store, repo_id, version,
                                 file_id, &data, &len) < 0) {
        syncw_warning ("[fs mgr] Failed to read file %s:%s.\n", repo_id, file_id);
        *io_error = TRUE;
        return FALSE;
    }

    gboolean ret = verify_syncwerk (file_id, data, len, verify_id, (version > 0));
    g_free (data);

    return ret;
}

static gboolean
verify_fs_object_v0 (const char *obj_id,
                     uint8_t *data,
                     int len,
                     gboolean verify_id)
{
    gboolean ret = TRUE;

    int type = syncw_metadata_type_from_data (obj_id, data, len, FALSE);
    switch (type) {
    case SYNCW_METADATA_TYPE_FILE:
        ret = verify_syncwerk_v0 (obj_id, data, len, verify_id);
        break;
    case SYNCW_METADATA_TYPE_DIR:
        ret = verify_syncwdir_v0 (obj_id, data, len, verify_id);
        break;
    default:
        syncw_warning ("Invalid meta data type: %d.\n", type);
        return FALSE;
    }

    return ret;
}

gboolean
syncw_fs_manager_verify_object (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *obj_id,
                               gboolean verify_id,
                               gboolean *io_error)
{
    void *data;
    int len;
    gboolean ret = TRUE;

    if (memcmp (obj_id, EMPTY_SHA1, 40) == 0) {
        return TRUE;
    }

    if (syncw_obj_store_read_obj (mgr->obj_store, repo_id, version,
                                 obj_id, &data, &len) < 0) {
        syncw_warning ("[fs mgr] Failed to read object %s:%s.\n", repo_id, obj_id);
        *io_error = TRUE;
        return FALSE;
    }

    if (version == 0)
        ret = verify_fs_object_v0 (obj_id, data, len, verify_id);
    else
        ret = verify_fs_object_json (obj_id, data, len);

    g_free (data);
    return ret;
}

int
dir_version_from_repo_version (int repo_version)
{
    if (repo_version == 0)
        return 0;
    else
        return CURRENT_DIR_OBJ_VERSION;
}

int
syncwerk_version_from_repo_version (int repo_version)
{
    if (repo_version == 0)
        return 0;
    else
        return CURRENT_SYNCWERK_OBJ_VERSION;
}

int
syncw_fs_manager_remove_store (SyncwFSManager *mgr,
                              const char *store_id)
{
    return syncw_obj_store_remove_store (mgr->obj_store, store_id);
}

GObject *
syncw_fs_manager_get_file_count_info_by_path (SyncwFSManager *mgr,
                                             const char *repo_id,
                                             int version,
                                             const char *root_id,
                                             const char *path,
                                             GError **error)
{
    char *dir_id = NULL;
    gint64 file_count = 0, dir_count = 0, size = 0;
    SyncwerkFileCountInfo *info = NULL;

    dir_id = syncw_fs_manager_get_syncwdir_id_by_path (mgr,
                                                     repo_id,
                                                     version,
                                                     root_id,
                                                     path, NULL);
    if (!dir_id) {
        syncw_warning ("Path %s doesn't exist or is not a dir in repo %.10s.\n",
                      path, repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad path");
        goto out;
    }
    if (get_file_count_info (mgr, repo_id, version,
                             dir_id, &dir_count, &file_count, &size) < 0) {
        syncw_warning ("Failed to get count info from path %s in repo %.10s.\n",
                      path, repo_id);
        goto out;
    }
    info = g_object_new (SYNCWERK_TYPE_FILE_COUNT_INFO,
                         "file_count", file_count,
                         "dir_count", dir_count,
                         "size", size, NULL);
out:
    g_free (dir_id);

    return (GObject *)info;
}
