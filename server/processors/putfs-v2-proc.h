/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_PUTFS_V2_PROC_H
#define SYNCWERK_PUTFS_V2_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_PUTFS_V2_PROC                  (syncwerk_putfs_v2_proc_get_type ())
#define SYNCWERK_PUTFS_V2_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_PUTFS_V2_PROC, SyncwerkPutfsV2Proc))
#define SYNCWERK_IS_PUTFS_V2_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_PUTFS_V2_PROC))
#define SYNCWERK_PUTFS_V2_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_PUTFS_V2_PROC, SyncwerkPutfsV2ProcClass))
#define IS_SYNCWERK_PUTFS_V2_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_PUTFS_V2_PROC))
#define SYNCWERK_PUTFS_V2_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_PUTFS_V2_PROC, SyncwerkPutfsV2ProcClass))

typedef struct _SyncwerkPutfsV2Proc SyncwerkPutfsV2Proc;
typedef struct _SyncwerkPutfsV2ProcClass SyncwerkPutfsV2ProcClass;

struct _SyncwerkPutfsV2Proc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkPutfsV2ProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_putfs_v2_proc_get_type ();

#endif

