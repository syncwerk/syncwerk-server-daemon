/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_RECVFS_PROC_H
#define SYNCWERK_RECVFS_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_RECVFS_PROC                  (syncwerk_recvfs_proc_get_type ())
#define SYNCWERK_RECVFS_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_RECVFS_PROC, SyncwerkRecvfsProc))
#define SYNCWERK_IS_RECVFS_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_RECVFS_PROC))
#define SYNCWERK_RECVFS_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_RECVFS_PROC, SyncwerkRecvfsProcClass))
#define IS_SYNCWERK_RECVFS_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_RECVFS_PROC))
#define SYNCWERK_RECVFS_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_RECVFS_PROC, SyncwerkRecvfsProcClass))

typedef struct _SyncwerkRecvfsProc SyncwerkRecvfsProc;
typedef struct _SyncwerkRecvfsProcClass SyncwerkRecvfsProcClass;

struct _SyncwerkRecvfsProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkRecvfsProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_recvfs_proc_get_type ();

#endif

