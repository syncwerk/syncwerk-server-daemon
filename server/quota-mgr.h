/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef QUOTA_MGR_H
#define QUOTA_MGR_H

#define INFINITE_QUOTA (gint64)-2

struct _SyncwQuotaManager {
    struct _SyncwerkSession *session;

    gboolean calc_share_usage;
};
typedef struct _SyncwQuotaManager SyncwQuotaManager;

SyncwQuotaManager *
syncw_quota_manager_new (struct _SyncwerkSession *session);

int
syncw_quota_manager_init (SyncwQuotaManager *mgr);

/* Set/get quota for a personal account. */
int
syncw_quota_manager_set_user_quota (SyncwQuotaManager *mgr,
                                   const char *user,
                                   gint64 quota);

gint64
syncw_quota_manager_get_user_quota (SyncwQuotaManager *mgr,
                                   const char *user);

gint64
syncw_quota_manager_get_user_share_usage (SyncwQuotaManager *mgr,
                                         const char *user);

/*
 * Check if @repo_id still has free space for upload.
 */
int
syncw_quota_manager_check_quota (SyncwQuotaManager *mgr,
                                const char *repo_id);

// ret = 0 means doesn't exceed quota,
// 1 means exceed quota,
// -1 means internal error
int
syncw_quota_manager_check_quota_with_delta (SyncwQuotaManager *mgr,
                                           const char *repo_id,
                                           gint64 delta);

gint64
syncw_quota_manager_get_user_usage (SyncwQuotaManager *mgr, const char *user);

#endif
