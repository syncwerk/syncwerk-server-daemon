/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_RECVCOMMIT_V3_PROC_H
#define SYNCWERK_RECVCOMMIT_V3_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_RECVCOMMIT_V3_PROC                  (syncwerk_recvcommit_v3_proc_get_type ())
#define SYNCWERK_RECVCOMMIT_V3_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_RECVCOMMIT_V3_PROC, SyncwerkRecvcommitV3Proc))
#define SYNCWERK_IS_RECVCOMMIT_V3_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_RECVCOMMIT_V3_PROC))
#define SYNCWERK_RECVCOMMIT_V3_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_RECVCOMMIT_V3_PROC, SyncwerkRecvcommitV3ProcClass))
#define IS_SYNCWERK_RECVCOMMIT_V3_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_RECVCOMMIT_V3_PROC))
#define SYNCWERK_RECVCOMMIT_V3_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_RECVCOMMIT_V3_PROC, SyncwerkRecvcommitV3ProcClass))

typedef struct _SyncwerkRecvcommitV3Proc SyncwerkRecvcommitV3Proc;
typedef struct _SyncwerkRecvcommitV3ProcClass SyncwerkRecvcommitV3ProcClass;

struct _SyncwerkRecvcommitV3Proc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkRecvcommitV3ProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_recvcommit_v3_proc_get_type ();

#endif
