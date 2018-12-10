/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_PUTREPOEMAILTOKEN_PROC_H
#define SYNCWERK_PUTREPOEMAILTOKEN_PROC_H

#include <glib-object.h>


#define SYNCWERK_TYPE_PUTREPOEMAILTOKEN_PROC                  (syncwerk_putrepoemailtoken_proc_get_type ())
#define SYNCWERK_PUTREPOEMAILTOKEN_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_PUTREPOEMAILTOKEN_PROC, SyncwerkPutrepoemailtokenProc))
#define SYNCWERK_IS_PUTREPOEMAILTOKEN_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_PUTREPOEMAILTOKEN_PROC))
#define SYNCWERK_PUTREPOEMAILTOKEN_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_PUTREPOEMAILTOKEN_PROC, SyncwerkPutrepoemailtokenProcClass))
#define IS_SYNCWERK_PUTREPOEMAILTOKEN_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_PUTREPOEMAILTOKEN_PROC))
#define SYNCWERK_PUTREPOEMAILTOKEN_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_PUTREPOEMAILTOKEN_PROC, SyncwerkPutrepoemailtokenProcClass))

typedef struct _SyncwerkPutrepoemailtokenProc SyncwerkPutrepoemailtokenProc;
typedef struct _SyncwerkPutrepoemailtokenProcClass SyncwerkPutrepoemailtokenProcClass;

struct _SyncwerkPutrepoemailtokenProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkPutrepoemailtokenProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_putrepoemailtoken_proc_get_type ();

#endif

