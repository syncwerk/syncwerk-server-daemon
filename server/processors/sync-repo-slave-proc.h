/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SYNCWERK_SYNC_REPO_SLAVE_PROC_H
#define SYNCWERK_SYNC_REPO_SLAVE_PROC_H

#include <glib-object.h>
#include <ccnet.h>

#define SYNCWERK_TYPE_SYNC_REPO_SLAVE_PROC                  (syncwerk_sync_repo_slave_proc_get_type ())
#define SYNCWERK_SYNC_REPO_SLAVE_PROC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYNCWERK_TYPE_SYNC_REPO_SLAVE_PROC, SyncwerkSynRepoSlaveProc))
#define SYNCWERK_IS_SYNC_REPO_SLAVE_PROC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYNCWERK_TYPE_SYNC_REPO_SLAVE_PROC))
#define SYNCWERK_SYNC_REPO_SLAVE_PROC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SYNCWERK_TYPE_SYNC_REPO_SLAVE_PROC, SyncwerkSynRepoSlaveProcClass))
#define IS_SYNCWERK_SYNC_REPO_SLAVE_PROC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SYNCWERK_TYPE_SYNC_REPO_SLAVE_PROC))
#define SYNCWERK_SYNC_REPO_SLAVE_PROC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCWERK_TYPE_SYNC_REPO_SLAVE_PROC, SyncwerkSynRepoSlaveProcClass))

typedef struct _SyncwerkSynRepoSlaveProc SyncwerkSynRepoSlaveProc;
typedef struct _SyncwerkSynRepoSlaveProcClass SyncwerkSynRepoSlaveProcClass;

struct _SyncwerkSynRepoSlaveProc {
    CcnetProcessor parent_instance;
};

struct _SyncwerkSynRepoSlaveProcClass {
    CcnetProcessorClass parent_class;
};

GType syncwerk_sync_repo_slave_proc_get_type ();

#endif
