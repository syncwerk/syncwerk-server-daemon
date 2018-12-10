/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_PUTCS_PROC_H
#define SYNCWERK_PUTCS_PROC_H

#include <glib-object.h>
#include <ccnet/processor.h>

#define SYNCWERK_TYPE_PUTCS_PROC                  (syncwerk_putcs_proc_get_type ())
#define SYNCWERK_PUTCS_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_PUTCS_PROC, SyncwerkPutcsProc))
#define SYNCWERK_IS_PUTCS_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_PUTCS_PROC))
#define SYNCWERK_PUTCS_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_PUTCS_PROC, SyncwerkPutcsProcClass))
#define IS_SYNCWERK_PUTCS_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_PUTCS_PROC))
#define SYNCWERK_PUTCS_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_PUTCS_PROC, SyncwerkPutcsProcClass))

typedef struct _SyncwerkPutcsProc SyncwerkPutcsProc;
typedef struct _SyncwerkPutcsProcClass SyncwerkPutcsProcClass;

struct _SyncwerkPutcsProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkPutcsProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_putcs_proc_get_type ();

#endif

