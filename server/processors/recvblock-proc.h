/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_RECVBLOCK_PROC_H
#define SYNCWERK_RECVBLOCK_PROC_H

#include <glib-object.h>

#define SYNCWERK_TYPE_RECVBLOCK_PROC                  (syncwerk_recvblock_proc_get_type ())
#define SYNCWERK_RECVBLOCK_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_RECVBLOCK_PROC, SyncwerkRecvblockProc))
#define SYNCWERK_IS_RECVBLOCK_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_RECVBLOCK_PROC))
#define SYNCWERK_RECVBLOCK_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_RECVBLOCK_PROC, SyncwerkRecvblockProcClass))
#define IS_SYNCWERK_RECVBLOCK_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_RECVBLOCK_PROC))
#define SYNCWERK_RECVBLOCK_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_RECVBLOCK_PROC, SyncwerkRecvblockProcClass))

typedef struct _SyncwerkRecvblockProc SyncwerkRecvblockProc;
typedef struct _SyncwerkRecvblockProcClass SyncwerkRecvblockProcClass;

struct _SyncwerkRecvblockProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkRecvblockProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_recvblock_proc_get_type ();

#endif
