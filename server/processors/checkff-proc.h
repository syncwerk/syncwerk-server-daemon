/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_CHECKFF_PROC_H
#define SYNCWERK_CHECKFF_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_CHECKFF_PROC                  (syncwerk_checkff_proc_get_type ())
#define SYNCWERK_CHECKFF_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_CHECKFF_PROC, SyncwerkCheckffProc))
#define SYNCWERK_IS_CHECKFF_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_CHECKFF_PROC))
#define SYNCWERK_CHECKFF_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_CHECKFF_PROC, SyncwerkCheckffProcClass))
#define IS_SYNCWERK_CHECKFF_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_CHECKFF_PROC))
#define SYNCWERK_CHECKFF_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_CHECKFF_PROC, SyncwerkCheckffProcClass))

typedef struct _SyncwerkCheckffProc SyncwerkCheckffProc;
typedef struct _SyncwerkCheckffProcClass SyncwerkCheckffProcClass;

struct _SyncwerkCheckffProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkCheckffProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_checkff_proc_get_type ();

#endif

