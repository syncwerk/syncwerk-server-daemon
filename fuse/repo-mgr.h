/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCW_REPO_MGR_H
#define SYNCW_REPO_MGR_H

#include <pthread.h>

#include "syncwerk-object.h"
#include "commit-mgr.h"
#include "branch-mgr.h"

struct _SyncwRepoManager;
typedef struct _SyncwRepo SyncwRepo;

struct _SyncwRepo {
    struct _SyncwRepoManager *manager;

    gchar       id[37];
    gchar      *name;
    gchar      *desc;
    gchar      *category;       /* not used yet */
    gboolean    encrypted;
    int         enc_version;
    gchar       magic[33];       /* hash(repo_id + passwd), key stretched. */
    gboolean    no_local_history;

    SyncwBranch *head;

    gboolean    is_corrupted;
    gboolean    delete_pending;
    int         ref_cnt;

    int version;
    /* Used to access fs and block sotre.
     * This id is different from repo_id when this repo is virtual.
     * Virtual repos share fs and block store with its origin repo.
     * However, commit store for each repo is always independent.
     * So always use repo_id to access commit store.
     */
    gchar       store_id[37];
};

gboolean is_repo_id_valid (const char *id);

SyncwRepo* 
syncw_repo_new (const char *id, const char *name, const char *desc);

void
syncw_repo_free (SyncwRepo *repo);

void
syncw_repo_ref (SyncwRepo *repo);

void
syncw_repo_unref (SyncwRepo *repo);

typedef struct _SyncwRepoManager SyncwRepoManager;
typedef struct _SyncwRepoManagerPriv SyncwRepoManagerPriv;

struct _SyncwRepoManager {
    struct _SyncwerkSession *syncw;

    SyncwRepoManagerPriv *priv;
};

SyncwRepoManager* 
syncw_repo_manager_new (struct _SyncwerkSession *syncw);

int
syncw_repo_manager_init (SyncwRepoManager *mgr);

int
syncw_repo_manager_start (SyncwRepoManager *mgr);

int
syncw_repo_manager_add_repo (SyncwRepoManager *mgr, SyncwRepo *repo);

int
syncw_repo_manager_del_repo (SyncwRepoManager *mgr, SyncwRepo *repo);

SyncwRepo* 
syncw_repo_manager_get_repo (SyncwRepoManager *manager, const gchar *id);

gboolean
syncw_repo_manager_repo_exists (SyncwRepoManager *manager, const gchar *id);

GList* 
syncw_repo_manager_get_repo_list (SyncwRepoManager *mgr, int start, int limit);

GList *
syncw_repo_manager_get_repo_id_list (SyncwRepoManager *mgr);

GList *
syncw_repo_manager_get_repos_by_owner (SyncwRepoManager *mgr,
                                      const char *email);

gboolean
syncw_repo_manager_is_virtual_repo (SyncwRepoManager *mgr, const char *repo_id);

#endif
