/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_RECVCOMMIT_PROC_H
#define SYNCWERK_RECVCOMMIT_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_RECVCOMMIT_PROC                  (syncwerk_recvcommit_proc_get_type ())
#define SYNCWERK_RECVCOMMIT_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_RECVCOMMIT_PROC, SyncwerkRecvcommitProc))
#define SYNCWERK_IS_RECVCOMMIT_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_RECVCOMMIT_PROC))
#define SYNCWERK_RECVCOMMIT_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_RECVCOMMIT_PROC, SyncwerkRecvcommitProcClass))
#define IS_SYNCWERK_RECVCOMMIT_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_RECVCOMMIT_PROC))
#define SYNCWERK_RECVCOMMIT_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_RECVCOMMIT_PROC, SyncwerkRecvcommitProcClass))

typedef struct _SyncwerkRecvcommitProc SyncwerkRecvcommitProc;
typedef struct _SyncwerkRecvcommitProcClass SyncwerkRecvcommitProcClass;

struct _SyncwerkRecvcommitProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkRecvcommitProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_recvcommit_proc_get_type ();

#endif
