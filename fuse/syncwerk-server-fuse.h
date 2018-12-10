#ifndef SYNCW_FUSE_H
#define SYNCW_FUSE_H

#include "syncwerk-session.h"

int parse_fuse_path (const char *path,
                     int *n_parts, char **user, char **repo_id, char **repo_path);

SyncwDirent *
fuse_get_dirent_by_path (SyncwFSManager *mgr,
                         const char *repo_id,
                         int version,
                         const char *root_id,
                         const char *path);

/* file.c */
int read_file(SyncwerkSession *syncw, const char *store_id, int version,
              Syncwerk *file, char *buf, size_t size,
              off_t offset, struct fuse_file_info *info);

/* getattr.c */
int do_getattr(SyncwerkSession *syncw, const char *path, struct stat *stbuf);

/* readdir.c */
int do_readdir(SyncwerkSession *syncw, const char *path, void *buf,
               fuse_fill_dir_t filler, off_t offset,
               struct fuse_file_info *info);

#endif /* SYNCW_FUSE_H */
