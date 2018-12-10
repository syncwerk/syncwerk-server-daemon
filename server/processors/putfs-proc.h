/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_PUTFS_PROC_H
#define SYNCWERK_PUTFS_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_PUTFS_PROC                  (syncwerk_putfs_proc_get_type ())
#define SYNCWERK_PUTFS_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_PUTFS_PROC, SyncwerkPutfsProc))
#define SYNCWERK_IS_PUTFS_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_PUTFS_PROC))
#define SYNCWERK_PUTFS_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_PUTFS_PROC, SyncwerkPutfsProcClass))
#define IS_SYNCWERK_PUTFS_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_PUTFS_PROC))
#define SYNCWERK_PUTFS_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_PUTFS_PROC, SyncwerkPutfsProcClass))

typedef struct _SyncwerkPutfsProc SyncwerkPutfsProc;
typedef struct _SyncwerkPutfsProcClass SyncwerkPutfsProcClass;

struct _SyncwerkPutfsProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkPutfsProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_putfs_proc_get_type ();

#endif

