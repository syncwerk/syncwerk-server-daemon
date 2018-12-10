/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCW_PERM_MGR_H
#define SYNCW_PERM_MGR_H

#include <glib.h>

struct _SyncwerkSession;

typedef struct _SyncwPermManager SyncwPermManager;
typedef struct _SyncwPermManagerPriv SyncwPermManagerPriv;

struct _SyncwPermManager {
    struct _SyncwerkSession *syncw;

    SyncwPermManagerPriv *priv;
};

SyncwPermManager*
syncw_perm_manager_new (struct _SyncwerkSession *syncw);

int
syncw_perm_manager_init (SyncwPermManager *mgr);

int
syncw_perm_manager_set_repo_owner (SyncwPermManager *mgr,
                                  const char *repo_id,
                                  const char *user_id);

char *
syncw_perm_manager_get_repo_owner (SyncwPermManager *mgr,
                                  const char *repo_id);

/* TODO: add start and limit. */
/* Get repos owned by this user.
 */
GList *
syncw_perm_manager_get_repos_by_owner (SyncwPermManager *mgr,
                                      const char *user_id);

#endif
