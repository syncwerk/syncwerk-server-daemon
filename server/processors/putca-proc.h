/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_PUTCA_PROC_H
#define SYNCWERK_PUTCA_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_PUTCA_PROC                  (syncwerk_putca_proc_get_type ())
#define SYNCWERK_PUTCA_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_PUTCA_PROC, SyncwerkPutcaProc))
#define SYNCWERK_IS_PUTCA_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_PUTCA_PROC))
#define SYNCWERK_PUTCA_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_PUTCA_PROC, SyncwerkPutcaProcClass))
#define IS_SYNCWERK_PUTCA_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_PUTCA_PROC))
#define SYNCWERK_PUTCA_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_PUTCA_PROC, SyncwerkPutcaProcClass))

typedef struct _SyncwerkPutcaProc SyncwerkPutcaProc;
typedef struct _SyncwerkPutcaProcClass SyncwerkPutcaProcClass;

struct _SyncwerkPutcaProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkPutcaProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_putca_proc_get_type ();

#endif
