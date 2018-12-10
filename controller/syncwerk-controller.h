/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 * Syncwerk-controller is responsible for:
 *
 *    1. Start: start server processes:
 *
 *       - syncwerk-server-ccnet
 *       - syncwerk-server-daemon
 *       - syncw-mon
 *
 *    2. Repair:
 *
 *       - ensure ccnet process availability by watching client->connfd
 *       - ensure server processes availablity by checking process is running periodically
 *         If some process has stopped working, try to restart it.
 *
 */

#ifndef SYNCWERK_CONTROLLER_H
#define SYNCWERK_CONTROLLER_H

typedef struct _SyncwerkController SyncwerkController;

enum {
    PID_CCNET = 0,
    PID_SERVER,
    PID_SYNCWDAV,
    N_PID
};

typedef struct SyncwDavConfig {
    gboolean enabled;
    gboolean fastcgi;
    int port;
    char *host;

} SyncwDavConfig;

struct _SyncwerkController {
    char *central_config_dir;
    char *config_dir;
    char *syncwerk_dir;
    char *logdir;

    CcnetClient         *client;
    CcnetClient         *sync_client;
    CcnetMqclientProc   *mqclient_proc;

    guint               check_process_timer;
    guint               client_io_id;
    /* Decide whether to start syncwerk-server-daemon in cloud mode  */
    gboolean            cloud_mode;

    int                 pid[N_PID];
    char                *pidfile[N_PID];

    SyncwDavConfig       syncwdav_config;
};
#endif
