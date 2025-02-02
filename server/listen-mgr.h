#ifndef SYNCW_LISTEN_MANAGER_H
#define SYNCW_LISTEN_MANAGER_H

/**
 * We always listen on the same tcp port for block tx.
 *
 * This module listens on the port, and when a new connection comes in, tries
 * to read a 37-bit uuid(called a `token'), and deliveres the new socket to
 * the corresponding block tx processor by calling the callback it provides
 * when the token is registered.
 *
 * The socket accepted by listen-mgr may be closed by either:
 *
 *  1. By listen manager:
 *     - if timeout is reached or error occured when waiting for the token.
 *     - if a token is received, but no corresponding callback is found for
 *       this token.
 *  2. If a valid token is received, the socket would be passed to the
 *     processor who registered the token, and the processor is now
 *     responsible for closing the socket.
 */

typedef struct _SyncwListenManager       SyncwListenManager;
typedef struct _SyncwListenManagerPriv   SyncwListenManagerPriv;

struct _SyncwListenManager {
    int port;
    SyncwListenManagerPriv *priv;
};

struct _SyncwListenManager *
syncw_listen_manager_new (struct _SyncwerkSession *session);

int
syncw_listen_manager_start (SyncwListenManager *mgr);

typedef void (*ConnAcceptedCB) (evutil_socket_t, void *);

/**
 * Register a token to identify a client when it connects later.
 *
 * @cb: this callback would be called with the connection socket, or with -1 when timeout
 * @cb_arg: user supplied argument
 * @timeout_sec: must be a positive number, the token would be expired after
 * that many seconds.
 */
int
syncw_listen_manager_register_token (SyncwListenManager *mgr,
                                    const char *token,
                                    ConnAcceptedCB cb,
                                    void *cb_arg,
                                    int timeout_sec);

char *
syncw_listen_manager_generate_token (SyncwListenManager *mgr);

#endif
