/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SHARE_MGR_H
#define SHARE_MGR_H

#include <glib.h>

struct _SyncwerkSession;

typedef struct _SyncwShareManager SyncwShareManager;
typedef struct _SyncwShareManagerPriv SyncwShareManagerPriv;
typedef struct _ShareRepoInfo ShareRepoInfo;

struct _SyncwShareManager {
    struct _SyncwerkSession *syncw;

};

SyncwShareManager*
syncw_share_manager_new (struct _SyncwerkSession *syncw);

int
syncw_share_manager_start (SyncwShareManager *mgr);

int
syncw_share_manager_add_share (SyncwShareManager *mgr, const char *repo_id,
                              const char *from_email, const char *to_email,
                              const char *permission);

int
syncw_share_manager_set_subdir_perm_by_path (SyncwShareManager *mgr, const char *repo_id,
                                            const char *from_email, const char *to_email,
                                            const char *permission, const char *path);

int
syncw_share_manager_set_permission (SyncwShareManager *mgr, const char *repo_id,
                                   const char *from_email, const char *to_email,
                                   const char *permission);

GList*
syncw_share_manager_list_share_repos (SyncwShareManager *mgr, const char *email,
                                     const char *type, int start, int limit);

GList *
syncw_share_manager_list_shared_to (SyncwShareManager *mgr,
                                   const char *owner,
                                   const char *repo_id);

GList *
syncw_share_manager_list_repo_shared_to (SyncwShareManager *mgr,
                                        const char *owner,
                                        const char *repo_id,
                                        GError **error);

GList *
syncw_share_manager_list_repo_shared_group (SyncwShareManager *mgr,
                                           const char *from_email,
                                           const char *repo_id,
                                           GError **error);

int
syncw_share_manager_remove_share (SyncwShareManager *mgr, const char *repo_id,
                                 const char *from_email, const char *to_email);

int
syncw_share_manager_unshare_subdir (SyncwShareManager* mgr,
                                   const char *orig_repo_id,
                                   const char *path,
                                   const char *from_email,
                                   const char *to_email);


/* Remove all share info of a repo. */
int
syncw_share_manager_remove_repo (SyncwShareManager *mgr, const char *repo_id);

char *
syncw_share_manager_check_permission (SyncwShareManager *mgr,
                                     const char *repo_id,
                                     const char *email);

GHashTable *
syncw_share_manager_get_shared_sub_dirs (SyncwShareManager *mgr,
                                        const char *repo_id,
                                        const char *path);

int
syncw_share_manager_is_repo_shared (SyncwShareManager *mgr,
                                   const char *repo_id);

GObject *
syncw_get_shared_repo_by_path (SyncwRepoManager *mgr,
                              const char *repo_id,
                              const char *path,
                              const char *shared_to,
                              int is_org,
                              GError **error);
int
syncw_share_manager_unshare_group_subdir (SyncwShareManager* mgr,
                                         const char *repo_id,
                                         const char *path,
                                         const char *owner,
                                         int group_id);

gboolean
syncw_share_manager_repo_has_been_shared (SyncwShareManager* mgr,
                                         const char *repo_id,
                                         gboolean including_groups);

GList *
syncw_share_manager_org_get_shared_users_by_repo (SyncwShareManager* mgr,
                                                 int org_id,
                                                 const char *repo_id);

GList *
syncw_share_manager_get_shared_users_by_repo (SyncwShareManager* mgr,
                                             const char *repo_id);
#endif /* SHARE_MGR_H */

