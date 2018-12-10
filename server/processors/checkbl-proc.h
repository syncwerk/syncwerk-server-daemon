/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_CHECKBL_PROC_H
#define SYNCWERK_CHECKBL_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_CHECKBL_PROC                  (syncwerk_checkbl_proc_get_type ())
#define SYNCWERK_CHECKBL_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_CHECKBL_PROC, SyncwerkCheckblProc))
#define SYNCWERK_IS_CHECKBL_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_CHECKBL_PROC))
#define SYNCWERK_CHECKBL_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_CHECKBL_PROC, SyncwerkCheckblProcClass))
#define IS_SYNCWERK_CHECKBL_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_CHECKBL_PROC))
#define SYNCWERK_CHECKBL_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_CHECKBL_PROC, SyncwerkCheckblProcClass))

typedef struct _SyncwerkCheckblProc SyncwerkCheckblProc;
typedef struct _SyncwerkCheckblProcClass SyncwerkCheckblProcClass;

struct _SyncwerkCheckblProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkCheckblProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_checkbl_proc_get_type ();

#endif

