/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef _CDC_H
#define _CDC_H

#include <glib.h>
#include <stdint.h>

#ifdef HAVE_MD5
#include "md5.h"
#define get_checksum md5
#define CHECKSUM_LENGTH 16
#else
#include <openssl/sha.h>
#define get_checksum sha1
#define CHECKSUM_LENGTH 20
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct _CDCFileDescriptor;
struct _CDCDescriptor;
struct SyncwerkCrypt;

typedef int (*WriteblockFunc)(const char *repo_id,
                              int version,
                              struct _CDCDescriptor *chunk_descr,
                              struct SyncwerkCrypt *crypt,
                              uint8_t *checksum,
                              gboolean write_data);

/* define chunk file header and block entry */
typedef struct _CDCFileDescriptor {
    uint32_t block_min_sz;
    uint32_t block_max_sz;
    uint32_t block_sz;
    uint64_t file_size;

    uint32_t block_nr;
    uint8_t *blk_sha1s;
    int max_block_nr;
    uint8_t  file_sum[CHECKSUM_LENGTH];

    WriteblockFunc write_block;

    char repo_id[37];
    int version;
} CDCFileDescriptor;

typedef struct _CDCDescriptor {
    uint64_t offset;
    uint32_t len;
    uint8_t  checksum[CHECKSUM_LENGTH];
    char    *block_buf;
    int result;
} CDCDescriptor;

int file_chunk_cdc(int fd_src,
                   CDCFileDescriptor *file_descr,
                   struct SyncwerkCrypt *crypt,
                   gboolean write_data,
                   gint64 *indexed);

int filename_chunk_cdc(const char *filename,
                       CDCFileDescriptor *file_descr,
                       struct SyncwerkCrypt *crypt,
                       gboolean write_data,
                       gint64 *indexed);

void cdc_init ();

#endif
