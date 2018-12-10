/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_HEARTBEAT_PROC_H
#define SYNCWERK_HEARTBEAT_PROC_H

#include <glib-object.h>
#include <ccnet/processor.h>

#define SYNCWERK_TYPE_HEARTBEAT_PROC                  (syncwerk_heartbeat_proc_get_type ())
#define SYNCWERK_HEARTBEAT_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_HEARTBEAT_PROC, SyncwerkHeartbeatProc))
#define SYNCWERK_IS_HEARTBEAT_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_HEARTBEAT_PROC))
#define SYNCWERK_HEARTBEAT_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_HEARTBEAT_PROC, SyncwerkHeartbeatProcClass))
#define IS_SYNCWERK_HEARTBEAT_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_HEARTBEAT_PROC))
#define SYNCWERK_HEARTBEAT_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_HEARTBEAT_PROC, SyncwerkHeartbeatProcClass))

typedef struct _SyncwerkHeartbeatProc SyncwerkHeartbeatProc;
typedef struct _SyncwerkHeartbeatProcClass SyncwerkHeartbeatProcClass;

struct _SyncwerkHeartbeatProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkHeartbeatProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_heartbeat_proc_get_type ();

#endif

