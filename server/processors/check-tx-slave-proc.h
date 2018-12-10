/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_CHECK_TX_SLAVE_PROC_H
#define SYNCWERK_CHECK_TX_SLAVE_PROC_H

#include <glib-object.h>
#include <ccnet/processor.h>

#define SYNCWERK_TYPE_CHECK_TX_SLAVE_PROC                  (syncwerk_check_tx_slave_proc_get_type ())
#define SYNCWERK_CHECK_TX_SLAVE_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_CHECK_TX_SLAVE_PROC, SyncwerkCheckTxSlaveProc))
#define SYNCWERK_IS_CHECK_TX_SLAVE_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_CHECK_TX_SLAVE_PROC))
#define SYNCWERK_CHECK_TX_SLAVE_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_CHECK_TX_SLAVE_PROC, SyncwerkCheckTxSlaveProcClass))
#define IS_SYNCWERK_CHECK_TX_SLAVE_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_CHECK_TX_SLAVE_PROC))
#define SYNCWERK_CHECK_TX_SLAVE_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_CHECK_TX_SLAVE_PROC, SyncwerkCheckTxSlaveProcClass))

typedef struct _SyncwerkCheckTxSlaveProc SyncwerkCheckTxSlaveProc;
typedef struct _SyncwerkCheckTxSlaveProcClass SyncwerkCheckTxSlaveProcClass;

struct _SyncwerkCheckTxSlaveProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkCheckTxSlaveProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_check_tx_slave_proc_get_type ();

#endif

