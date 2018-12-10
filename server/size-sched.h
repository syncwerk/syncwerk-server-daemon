#ifndef SIZE_SCHEDULER_H
#define SIZE_SCHEDULER_H

struct _SyncwerkSession;

struct SizeSchedulerPriv;

typedef struct SizeScheduler {
    struct _SyncwerkSession *syncw;

    struct SizeSchedulerPriv *priv;
} SizeScheduler;

SizeScheduler *
size_scheduler_new (struct _SyncwerkSession *session);

int
size_scheduler_start (SizeScheduler *scheduler);

void
schedule_repo_size_computation (SizeScheduler *scheduler, const char *repo_id);

#endif
