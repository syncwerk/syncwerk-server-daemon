/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_RECVBLOCK_V2_PROC_H
#define SYNCWERK_RECVBLOCK_V2_PROC_H

#include <glib-object.h>

#define SYNCWERK_TYPE_RECVBLOCK_V2_PROC                  (syncwerk_recvblock_v2_proc_get_type ())
#define SYNCWERK_RECVBLOCK_V2_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_RECVBLOCK_V2_PROC, SyncwerkRecvblockV2Proc))
#define SYNCWERK_IS_RECVBLOCK_V2_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_RECVBLOCK_V2_PROC))
#define SYNCWERK_RECVBLOCK_V2_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_RECVBLOCK_V2_PROC, SyncwerkRecvblockV2ProcClass))
#define IS_SYNCWERK_RECVBLOCK_V2_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_RECVBLOCK_V2_PROC))
#define SYNCWERK_RECVBLOCK_V2_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_RECVBLOCK_V2_PROC, SyncwerkRecvblockV2ProcClass))

typedef struct _SyncwerkRecvblockV2Proc SyncwerkRecvblockV2Proc;
typedef struct _SyncwerkRecvblockV2ProcClass SyncwerkRecvblockV2ProcClass;

struct _SyncwerkRecvblockV2Proc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkRecvblockV2ProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_recvblock_v2_proc_get_type ();

#endif
