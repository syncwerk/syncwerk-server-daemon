/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_CHECK_PROTOCOL_SLAVE_PROC_H
#define SYNCWERK_CHECK_PROTOCOL_SLAVE_PROC_H

#include <glib-object.h>
#include <ccnet.h>

#define SYNCWERK_TYPE_CHECK_PROTOCOL_SLAVE_PROC                  (syncwerk_check_protocol_slave_proc_get_type ())
#define SYNCWERK_CHECK_PROTOCOL_SLAVE_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_CHECK_PROTOCOL_SLAVE_PROC, SyncwerkCheckProtocolSlaveProc))
#define SYNCWERK_IS_CHECK_PROTOCOL_SLAVE_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_CHECK_PROTOCOL_SLAVE_PROC))
#define SYNCWERK_CHECK_PROTOCOL_SLAVE_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_CHECK_PROTOCOL_SLAVE_PROC, SyncwerkCheckProtocolSlaveProcClass))
#define IS_SYNCWERK_CHECK_PROTOCOL_SLAVE_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_CHECK_PROTOCOL_SLAVE_PROC))
#define SYNCWERK_CHECK_PROTOCOL_SLAVE_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_CHECK_PROTOCOL_SLAVE_PROC, SyncwerkCheckProtocolSlaveProcClass))

typedef struct _SyncwerkCheckProtocolSlaveProc SyncwerkCheckProtocolSlaveProc;
typedef struct _SyncwerkCheckProtocolSlaveProcClass SyncwerkCheckProtocolSlaveProcClass;

struct _SyncwerkCheckProtocolSlaveProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkCheckProtocolSlaveProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_check_protocol_slave_proc_get_type ();

#endif
