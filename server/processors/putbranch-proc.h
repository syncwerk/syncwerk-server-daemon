/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_PUTBRANCH_PROC_H
#define SYNCWERK_PUTBRANCH_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_PUTBRANCH_PROC                  (syncwerk_putbranch_proc_get_type ())
#define SYNCWERK_PUTBRANCH_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_PUTBRANCH_PROC, SyncwerkPutbranchProc))
#define SYNCWERK_IS_PUTBRANCH_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_PUTBRANCH_PROC))
#define SYNCWERK_PUTBRANCH_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_PUTBRANCH_PROC, SyncwerkPutbranchProcClass))
#define IS_SYNCWERK_PUTBRANCH_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_PUTBRANCH_PROC))
#define SYNCWERK_PUTBRANCH_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_PUTBRANCH_PROC, SyncwerkPutbranchProcClass))

typedef struct _SyncwerkPutbranchProc SyncwerkPutbranchProc;
typedef struct _SyncwerkPutbranchProcClass SyncwerkPutbranchProcClass;

struct _SyncwerkPutbranchProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkPutbranchProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_putbranch_proc_get_type ();

#endif

