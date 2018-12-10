/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_CHECK_TX_SLAVE_V3_PROC_H
#define SYNCWERK_CHECK_TX_SLAVE_V3_PROC_H

#include <glib-object.h>
#include <ccnet/processor.h>

#define SYNCWERK_TYPE_CHECK_TX_SLAVE_V3_PROC                  (syncwerk_check_tx_slave_v3_proc_get_type ())
#define SYNCWERK_CHECK_TX_SLAVE_V3_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_CHECK_TX_SLAVE_V3_PROC, SyncwerkCheckTxSlaveV3Proc))
#define SYNCWERK_IS_CHECK_TX_SLAVE_V3_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_CHECK_TX_SLAVE_V3_PROC))
#define SYNCWERK_CHECK_TX_SLAVE_V3_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_CHECK_TX_SLAVE_V3_PROC, SyncwerkCheckTxSlaveV3ProcClass))
#define IS_SYNCWERK_CHECK_TX_SLAVE_V3_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_CHECK_TX_SLAVE_V3_PROC))
#define SYNCWERK_CHECK_TX_SLAVE_V3_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_CHECK_TX_SLAVE_V3_PROC, SyncwerkCheckTxSlaveV3ProcClass))

typedef struct _SyncwerkCheckTxSlaveV3Proc SyncwerkCheckTxSlaveV3Proc;
typedef struct _SyncwerkCheckTxSlaveV3ProcClass SyncwerkCheckTxSlaveV3ProcClass;

struct _SyncwerkCheckTxSlaveV3Proc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkCheckTxSlaveV3ProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_check_tx_slave_v3_proc_get_type ();

#endif
