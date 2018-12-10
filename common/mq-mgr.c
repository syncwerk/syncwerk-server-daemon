/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <ccnet.h>

#include "mq-mgr.h"

#include "syncwerk-session.h"
#include "log.h"

typedef struct _SyncwMqManagerPriv SyncwMqManagerPriv;

struct _SyncwMqManagerPriv {
    CcnetMqclientProc *mqclient_proc;
    CcnetTimer *timer; 
};

SyncwMqManager *
syncw_mq_manager_new (SyncwerkSession *syncw)
{
    CcnetClient *client = syncw->session;
    SyncwMqManager *mgr;
    SyncwMqManagerPriv *priv;

    mgr = g_new0 (SyncwMqManager, 1);
    priv = g_new0 (SyncwMqManagerPriv, 1);
    
    
    mgr->syncw = syncw;
    mgr->priv = priv;

    priv->mqclient_proc = (CcnetMqclientProc *)
        ccnet_proc_factory_create_master_processor (client->proc_factory,
                                                    "mq-client");

    if (!priv->mqclient_proc) {
        syncw_warning ("Failed to create mqclient proc.\n");
        g_free (mgr);
        g_free(priv);
        return NULL;
    }

    return mgr;
}

static int
start_mq_client (CcnetMqclientProc *mqclient)
{
    if (ccnet_processor_startl ((CcnetProcessor *)mqclient, NULL) < 0) {
        ccnet_processor_done ((CcnetProcessor *)mqclient, FALSE);
        syncw_warning ("Failed to start mqclient proc\n");
        return -1;
    }

    syncw_message ("[mq client] mq cilent is started\n");

    return 0;
}

int
syncw_mq_manager_init (SyncwMqManager *mgr)
{
    SyncwMqManagerPriv *priv = mgr->priv;
    if (start_mq_client(priv->mqclient_proc) < 0)
        return -1;
    return 0;
}

int
syncw_mq_manager_start (SyncwMqManager *mgr)
{
    return 0;
}

static inline CcnetMessage *
create_message (SyncwMqManager *mgr, const char *app, const char *body, int flags)
{
    CcnetClient *client = mgr->syncw->session;
    CcnetMessage *msg;
    
    char *from = client->base.id;
    char *to = client->base.id;

    msg = ccnet_message_new (from, to, app, body, flags);
    return msg;
}

/* Wrap around ccnet_message_new since all messages we use are local. */
static inline void
_send_message (SyncwMqManager *mgr, CcnetMessage *msg)
{
    CcnetMqclientProc *mqclient_proc = mgr->priv->mqclient_proc;
    ccnet_mqclient_proc_put_message (mqclient_proc, msg);
}

void
syncw_mq_manager_publish_message (SyncwMqManager *mgr,
                                 CcnetMessage *msg)
{
    _send_message (mgr, msg);
}

void
syncw_mq_manager_publish_message_full (SyncwMqManager *mgr,
                                      const char *app,
                                      const char *body,
                                      int flags)
{
    CcnetMessage *msg = create_message (mgr, app, body, flags);
    _send_message (mgr, msg);
    ccnet_message_free (msg);
}

void
syncw_mq_manager_publish_notification (SyncwMqManager *mgr,
                                      const char *type,
                                      const char *content)
{
    static const char *app = "syncwerk.notification";
    
    GString *buf = g_string_new(NULL);
    g_string_append_printf (buf, "%s\n%s", type, content);
    
    CcnetMessage *msg = create_message (mgr, app, buf->str, 0);
    _send_message (mgr, msg);
    
    g_string_free (buf, TRUE);
    ccnet_message_free (msg);
}

void
syncw_mq_manager_publish_event (SyncwMqManager *mgr, const char *content)
{
    static const char *app = "syncwerk_server_daemon.event";

    CcnetMessage *msg = create_message (mgr, app, content, 0);
    _send_message (mgr, msg);

    ccnet_message_free (msg);
}

void
syncw_mq_manager_publish_stats_event (SyncwMqManager *mgr, const char *content)
{
    static const char *app = "syncwerk_server_daemon.stats";

    CcnetMessage *msg = create_message (mgr, app, content, 0);
    _send_message (mgr, msg);

    ccnet_message_free (msg);
}
