/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 * Mq-manager is responsible for: 
 * 
 *  - Publishing heartbeat messages every HEARTBEAT_INTERVAL senconds to
 *    indicate it's alive. If syncwerk-applet doesn't get the message, it would
 *    check and try to restart syncw-daemon.
 *
 *  - Provide API for other modules to publish their messages.
 *
 * Currently we publish these types of messages:
 *
 *  - syncwerk.heartbeat <>
 *  - syncwerk.transfer <start | stop >
 *  - syncwerk.repo_sync_done <repo-name>
 *  - syncwerk.promt_create_repo <worktree>
 *  - syncwerk.repo_created <repo-name>
 *
 * And subscribe to no messages. 
 */

#ifndef SYNCW_MQ_MANAGER_H
#define SYNCW_MQ_MANAGER_H

struct _CcnetMessage;

typedef struct _SyncwMqManager SyncwMqManager;

struct _SyncwMqManager {
    struct _SyncwerkSession   *syncw;
    struct _SyncwMqManagerPriv *priv;
};

SyncwMqManager *syncw_mq_manager_new (struct _SyncwerkSession *syncw);   

int syncw_mq_manager_init (SyncwMqManager *mgr);

int syncw_mq_manager_start (SyncwMqManager *mgr);


void syncw_mq_manager_publish_message (SyncwMqManager *mgr,
                                      struct _CcnetMessage *msg);

void
syncw_mq_manager_publish_message_full (SyncwMqManager *mgr,
                                      const char *app,
                                      const char *body,
                                      int flags);

void
syncw_mq_manager_publish_notification (SyncwMqManager *mgr,
                                      const char *type,
                                      const char *content);

void
syncw_mq_manager_publish_event (SyncwMqManager *mgr, const char *content);

void
syncw_mq_manager_publish_stats_event (SyncwMqManager *mgr, const char *content);

#endif
