/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCW_TOKEN_MGR_H
#define SYNCW_TOKEN_MGR_H

#include <rpcsyncwerk-client.h>

struct _SyncwerkSession;
struct TokenManagerPriv;

struct _SyncwTokenManager {
    struct _SyncwerkSession  *syncw;
    struct TokenManagerPriv *priv;
};
typedef struct _SyncwTokenManager SyncwTokenManager;

SyncwTokenManager *
syncw_token_manager_new (struct _SyncwerkSession *session);

/* Generate a token, signed by me.
 * This is called by a master server.
 */
char *
syncw_token_manager_generate_token (SyncwTokenManager *mgr,
                                   const char *client_id,
                                   const char *repo_id);

/* Verify whether a token is valid.
 *
 * @peer_id: the peer who presents this token to me.
 * If the token is valid, repo id will be stored in @ret_repo_id.
 */
int
syncw_token_manager_verify_token (SyncwTokenManager *mgr,
                                 RpcsyncwerkClient *rpc_client,
                                 const char *peer_id,
                                 char *token,
                                 char *ret_repo_id);

#if 0
/* Record a used token so that it cannot be reused.
 * This function should only be called after the token has been verified.
 */
void
syncw_token_manager_invalidate_token (SyncwTokenManager *mgr,
                                     char *token);
#endif

#endif
