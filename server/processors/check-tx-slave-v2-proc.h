/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_CHECK_TX_SLAVE_V2_PROC_H
#define SYNCWERK_CHECK_TX_SLAVE_V2_PROC_H

#include <glib-object.h>
#include <ccnet/processor.h>

#define SYNCWERK_TYPE_CHECK_TX_SLAVE_V2_PROC                  (syncwerk_check_tx_slave_v2_proc_get_type ())
#define SYNCWERK_CHECK_TX_SLAVE_V2_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_CHECK_TX_SLAVE_V2_PROC, SyncwerkCheckTxSlaveV2Proc))
#define SYNCWERK_IS_CHECK_TX_SLAVE_V2_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_CHECK_TX_SLAVE_V2_PROC))
#define SYNCWERK_CHECK_TX_SLAVE_V2_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_CHECK_TX_SLAVE_V2_PROC, SyncwerkCheckTxSlaveV2ProcClass))
#define IS_SYNCWERK_CHECK_TX_SLAVE_V2_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_CHECK_TX_SLAVE_V2_PROC))
#define SYNCWERK_CHECK_TX_SLAVE_V2_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_CHECK_TX_SLAVE_V2_PROC, SyncwerkCheckTxSlaveV2ProcClass))

typedef struct _SyncwerkCheckTxSlaveV2Proc SyncwerkCheckTxSlaveV2Proc;
typedef struct _SyncwerkCheckTxSlaveV2ProcClass SyncwerkCheckTxSlaveV2ProcClass;

struct _SyncwerkCheckTxSlaveV2Proc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkCheckTxSlaveV2ProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_check_tx_slave_v2_proc_get_type ();

#endif
