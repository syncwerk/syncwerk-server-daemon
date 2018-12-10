/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_PUTCOMMIT_V3_PROC_H
#define SYNCWERK_PUTCOMMIT_V3_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_PUTCOMMIT_V3_PROC                  (syncwerk_putcommit_v3_proc_get_type ())
#define SYNCWERK_PUTCOMMIT_V3_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_PUTCOMMIT_V3_PROC, SyncwerkPutcommitV3Proc))
#define SYNCWERK_IS_PUTCOMMIT_V3_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_PUTCOMMIT_V3_PROC))
#define SYNCWERK_PUTCOMMIT_V3_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_PUTCOMMIT_V3_PROC, SyncwerkPutcommitV3ProcClass))
#define IS_SYNCWERK_PUTCOMMIT_V3_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_PUTCOMMIT_V3_PROC))
#define SYNCWERK_PUTCOMMIT_V3_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_PUTCOMMIT_V3_PROC, SyncwerkPutcommitV3ProcClass))

typedef struct _SyncwerkPutcommitV3Proc SyncwerkPutcommitV3Proc;
typedef struct _SyncwerkPutcommitV3ProcClass SyncwerkPutcommitV3ProcClass;

struct _SyncwerkPutcommitV3Proc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkPutcommitV3ProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_putcommit_v3_proc_get_type ();

#endif
