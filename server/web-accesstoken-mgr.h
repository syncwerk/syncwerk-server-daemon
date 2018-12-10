/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef WEB_ACCESSTOKEN_MGR_H
#define WEB_ACCESSTOKEN_MGR_H

struct _SyncwerkSession;

struct WebATPriv;

struct _SyncwWebAccessTokenManager {
    struct _SyncwerkSession	*syncw;
    struct WebATPriv *priv;
};
typedef struct _SyncwWebAccessTokenManager SyncwWebAccessTokenManager;

SyncwWebAccessTokenManager* syncw_web_at_manager_new (struct _SyncwerkSession *syncw);

int
syncw_web_at_manager_start (SyncwWebAccessTokenManager *mgr);

/*
 * Returns an access token for the given access info.
 * If a token doesn't exist or has expired, generate and return a new one.
 */
char *
syncw_web_at_manager_get_access_token (SyncwWebAccessTokenManager *mgr,
                                      const char *repo_id,
                                      const char *obj_id,
                                      const char *op,
                                      const char *username,
                                      int use_onetime,
                                      GError **error);

/*
 * Returns access info for the given token.
 */
SyncwerkWebAccess *
syncw_web_at_manager_query_access_token (SyncwWebAccessTokenManager *mgr,
                                        const char *token);

#endif /* WEB_ACCESSTOKEN_MGR_H */

