/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_RECVBRANCH_PROC_H
#define SYNCWERK_RECVBRANCH_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_RECVBRANCH_PROC                  (syncwerk_recvbranch_proc_get_type ())
#define SYNCWERK_RECVBRANCH_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_RECVBRANCH_PROC, SyncwerkRecvbranchProc))
#define SYNCWERK_IS_RECVBRANCH_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_RECVBRANCH_PROC))
#define SYNCWERK_RECVBRANCH_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_RECVBRANCH_PROC, SyncwerkRecvbranchProcClass))
#define IS_SYNCWERK_RECVBRANCH_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_RECVBRANCH_PROC))
#define SYNCWERK_RECVBRANCH_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_RECVBRANCH_PROC, SyncwerkRecvbranchProcClass))

typedef struct _SyncwerkRecvbranchProc SyncwerkRecvbranchProc;
typedef struct _SyncwerkRecvbranchProcClass SyncwerkRecvbranchProcClass;

struct _SyncwerkRecvbranchProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkRecvbranchProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_recvbranch_proc_get_type ();

#endif

