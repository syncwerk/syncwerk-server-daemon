/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCW_FILE_MGR_H
#define SYNCW_FILE_MGR_H

#include <glib.h>

#include "syncwerk-object.h"

#include "obj-store.h"

#include "cdc/cdc.h"
#include "../common/syncwerk-crypt.h"

#define CURRENT_DIR_OBJ_VERSION 1
#define CURRENT_SYNCWERK_OBJ_VERSION 1

typedef struct _SyncwFSManager SyncwFSManager;
typedef struct _SyncwFSObject SyncwFSObject;
typedef struct _Syncwerk Syncwerk;
typedef struct _SyncwDir SyncwDir;
typedef struct _SyncwDirent SyncwDirent;

typedef enum {
    SYNCW_METADATA_TYPE_INVALID,
    SYNCW_METADATA_TYPE_FILE,
    SYNCW_METADATA_TYPE_LINK,
    SYNCW_METADATA_TYPE_DIR,
} SyncwMetadataType;

/* Common to syncwerk and syncwdir objects. */
struct _SyncwFSObject {
    int type;
};

struct _Syncwerk {
    SyncwFSObject object;
    int         version;
    char        file_id[41];
    guint64     file_size;
    guint32     n_blocks;
    char        **blk_sha1s;
    int         ref_count;
};

void
syncwerk_ref (Syncwerk *syncwerk);

void
syncwerk_unref (Syncwerk *syncwerk);

int
syncwerk_save (SyncwFSManager *fs_mgr,
              const char *repo_id,
              int version,
              Syncwerk *file);

#define SYNCW_DIR_NAME_LEN 256

struct _SyncwDirent {
    int        version;
    guint32    mode;
    char       id[41];
    guint32    name_len;
    char       *name;

    /* attributes for version > 0 */
    gint64     mtime;
    char       *modifier;       /* for files only */
    gint64     size;            /* for files only */
};

struct _SyncwDir {
    SyncwFSObject object;
    int    version;
    char   dir_id[41];
    GList *entries;

    /* data in on-disk format. */
    void  *ondisk;
    int    ondisk_size;
};

SyncwDir *
syncw_dir_new (const char *id, GList *entries, int version);

void 
syncw_dir_free (SyncwDir *dir);

SyncwDir *
syncw_dir_from_data (const char *dir_id, uint8_t *data, int len,
                    gboolean is_json);

void *
syncw_dir_to_data (SyncwDir *dir, int *len);

int 
syncw_dir_save (SyncwFSManager *fs_mgr,
               const char *repo_id,
               int version,
               SyncwDir *dir);

SyncwDirent *
syncw_dirent_new (int version, const char *sha1, int mode, const char *name,
                 gint64 mtime, const char *modifier, gint64 size);

void
syncw_dirent_free (SyncwDirent *dent);

SyncwDirent *
syncw_dirent_dup (SyncwDirent *dent);

int
syncw_metadata_type_from_data (const char *obj_id,
                              uint8_t *data, int len, gboolean is_json);

/* Parse an fs object without knowing its type. */
SyncwFSObject *
syncw_fs_object_from_data (const char *obj_id,
                          uint8_t *data, int len,
                          gboolean is_json);

void
syncw_fs_object_free (SyncwFSObject *obj);

typedef struct {
    /* TODO: GHashTable may be inefficient when we have large number of IDs. */
    GHashTable  *block_hash;
    GPtrArray   *block_ids;
    uint32_t     n_blocks;
    uint32_t     n_valid_blocks;
} BlockList;

BlockList *
block_list_new ();

void
block_list_free (BlockList *bl);

void
block_list_insert (BlockList *bl, const char *block_id);

/* Return a blocklist containing block ids which are in @bl1 but
 * not in @bl2.
 */
BlockList *
block_list_difference (BlockList *bl1, BlockList *bl2);

struct _SyncwerkSession;

typedef struct _SyncwFSManagerPriv SyncwFSManagerPriv;

struct _SyncwFSManager {
    struct _SyncwerkSession *syncw;

    struct SyncwObjStore *obj_store;

    SyncwFSManagerPriv *priv;
};

SyncwFSManager *
syncw_fs_manager_new (struct _SyncwerkSession *syncw,
                     const char *syncw_dir);

int
syncw_fs_manager_init (SyncwFSManager *mgr);

#ifndef SYNCWERK_SERVER

int 
syncw_fs_manager_checkout_file (SyncwFSManager *mgr, 
                               const char *repo_id,
                               int version,
                               const char *file_id, 
                               const char *file_path,
                               guint32 mode,
                               guint64 mtime,
                               struct SyncwerkCrypt *crypt,
                               const char *in_repo_path,
                               const char *conflict_head_id,
                               gboolean force_conflict,
                               gboolean *conflicted,
                               const char *email);

#endif  /* not SYNCWERK_SERVER */

/**
 * Check in blocks and create syncwerk/symlink object.
 * Returns sha1 id for the syncwerk/symlink object in @sha1 parameter.
 */
int
syncw_fs_manager_index_file_blocks (SyncwFSManager *mgr,
                                   const char *repo_id,
                                   int version,
                                   GList *paths,
                                   GList *blockids,
                                   unsigned char sha1[],
                                   gint64 file_size);

int
syncw_fs_manager_index_raw_blocks (SyncwFSManager *mgr,
                                  const char *repo_id,
                                  int version,
                                  GList *paths,
                                  GList *blockids);

int
syncw_fs_manager_index_existed_file_blocks (SyncwFSManager *mgr,
                                           const char *repo_id,
                                           int version,
                                           GList *blockids,
                                           unsigned char sha1[],
                                           gint64 file_size);
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
                              gint64 *indexed);

Syncwerk *
syncw_fs_manager_get_syncwerk (SyncwFSManager *mgr,
                             const char *repo_id,
                             int version,
                             const char *file_id);

SyncwDir *
syncw_fs_manager_get_syncwdir (SyncwFSManager *mgr,
                             const char *repo_id,
                             int version,
                             const char *dir_id);

/* Make sure entries in the returned dir is sorted in descending order.
 */
SyncwDir *
syncw_fs_manager_get_syncwdir_sorted (SyncwFSManager *mgr,
                                    const char *repo_id,
                                    int version,
                                    const char *dir_id);

SyncwDir *
syncw_fs_manager_get_syncwdir_sorted_by_path (SyncwFSManager *mgr,
                                            const char *repo_id,
                                            int version,
                                            const char *root_id,
                                            const char *path);

int
syncw_fs_manager_populate_blocklist (SyncwFSManager *mgr,
                                    const char *repo_id,
                                    int version,
                                    const char *root_id,
                                    BlockList *bl);

/*
 * For dir object, set *stop to TRUE to stop traversing the subtree.
 */
typedef gboolean (*TraverseFSTreeCallback) (SyncwFSManager *mgr,
                                            const char *repo_id,
                                            int version,
                                            const char *obj_id,
                                            int type,
                                            void *user_data,
                                            gboolean *stop);

int
syncw_fs_manager_traverse_tree (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *root_id,
                               TraverseFSTreeCallback callback,
                               void *user_data,
                               gboolean skip_errors);

typedef gboolean (*TraverseFSPathCallback) (SyncwFSManager *mgr,
                                            const char *path,
                                            SyncwDirent *dent,
                                            void *user_data,
                                            gboolean *stop);

int
syncw_fs_manager_traverse_path (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *root_id,
                               const char *dir_path,
                               TraverseFSPathCallback callback,
                               void *user_data);

gboolean
syncw_fs_manager_object_exists (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *id);

void
syncw_fs_manager_delete_object (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *id);

gint64
syncw_fs_manager_get_file_size (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *file_id);

gint64
syncw_fs_manager_get_fs_size (SyncwFSManager *mgr,
                             const char *repo_id,
                             int version,
                             const char *root_id);

#ifndef SYNCWERK_SERVER
int
syncwerk_write_chunk (const char *repo_id,
                     int version,
                     CDCDescriptor *chunk,
                     SyncwerkCrypt *crypt,
                     uint8_t *checksum,
                     gboolean write_data);
int
syncwerk_check_write_chunk (CDCDescriptor *chunk,
                           uint8_t *sha1,
                           gboolean write_data);
#endif /* SYNCWERK_SERVER */

uint32_t
calculate_chunk_size (uint64_t total_size);

int
syncw_fs_manager_count_fs_files (SyncwFSManager *mgr,
                                const char *repo_id,
                                int version,
                                const char *root_id);

SyncwDir *
syncw_fs_manager_get_syncwdir_by_path(SyncwFSManager *mgr,
                                    const char *repo_id,
                                    int version,
                                    const char *root_id,
                                    const char *path,
                                    GError **error);
char *
syncw_fs_manager_get_syncwerk_id_by_path (SyncwFSManager *mgr,
                                        const char *repo_id,
                                        int version,
                                        const char *root_id,
                                        const char *path,
                                        GError **error);

char *
syncw_fs_manager_path_to_obj_id (SyncwFSManager *mgr,
                                const char *repo_id,
                                int version,
                                const char *root_id,
                                const char *path,
                                guint32 *mode,
                                GError **error);

char *
syncw_fs_manager_get_syncwdir_id_by_path (SyncwFSManager *mgr,
                                        const char *repo_id,
                                        int version,
                                        const char *root_id,
                                        const char *path,
                                        GError **error);

SyncwDirent *
syncw_fs_manager_get_dirent_by_path (SyncwFSManager *mgr,
                                    const char *repo_id,
                                    int version,
                                    const char *root_id,
                                    const char *path,
                                    GError **error);

/* Check object integrity. */

gboolean
syncw_fs_manager_verify_syncwdir (SyncwFSManager *mgr,
                                const char *repo_id,
                                int version,
                                const char *dir_id,
                                gboolean verify_id,
                                gboolean *io_error);

gboolean
syncw_fs_manager_verify_syncwerk (SyncwFSManager *mgr,
                                const char *repo_id,
                                int version,
                                const char *file_id,
                                gboolean verify_id,
                                gboolean *io_error);

gboolean
syncw_fs_manager_verify_object (SyncwFSManager *mgr,
                               const char *repo_id,
                               int version,
                               const char *obj_id,
                               gboolean verify_id,
                               gboolean *io_error);

int
dir_version_from_repo_version (int repo_version);

int
syncwerk_version_from_repo_version (int repo_version);

struct _CDCFileDescriptor;
void
syncw_fs_manager_calculate_syncwerk_id_json (int repo_version,
                                           struct _CDCFileDescriptor *cdc,
                                           guint8 *file_id_sha1);

int
syncw_fs_manager_remove_store (SyncwFSManager *mgr,
                              const char *store_id);

GObject *
syncw_fs_manager_get_file_count_info_by_path (SyncwFSManager *mgr,
                                             const char *repo_id,
                                             int version,
                                             const char *root_id,
                                             const char *path,
                                             GError **error);

#endif
