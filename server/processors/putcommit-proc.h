/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_PUTCOMMIT_PROC_H
#define SYNCWERK_PUTCOMMIT_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_PUTCOMMIT_PROC                  (syncwerk_putcommit_proc_get_type ())
#define SYNCWERK_PUTCOMMIT_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_PUTCOMMIT_PROC, SyncwerkPutcommitProc))
#define SYNCWERK_IS_PUTCOMMIT_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_PUTCOMMIT_PROC))
#define SYNCWERK_PUTCOMMIT_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_PUTCOMMIT_PROC, SyncwerkPutcommitProcClass))
#define IS_SYNCWERK_PUTCOMMIT_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_PUTCOMMIT_PROC))
#define SYNCWERK_PUTCOMMIT_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_PUTCOMMIT_PROC, SyncwerkPutcommitProcClass))

typedef struct _SyncwerkPutcommitProc SyncwerkPutcommitProc;
typedef struct _SyncwerkPutcommitProcClass SyncwerkPutcommitProcClass;

struct _SyncwerkPutcommitProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkPutcommitProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_putcommit_proc_get_type ();

#endif
