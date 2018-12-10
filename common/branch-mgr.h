#ifndef SYNCW_BRANCH_MGR_H
#define SYNCW_BRANCH_MGR_H

#include "commit-mgr.h"
#define NO_BRANCH "-"

typedef struct _SyncwBranch SyncwBranch;

struct _SyncwBranch {
    int   ref;
    char *name;
    char  repo_id[37];
    char  commit_id[41];
};

SyncwBranch *syncw_branch_new (const char *name,
                             const char *repo_id,
                             const char *commit_id);
void syncw_branch_free (SyncwBranch *branch);
void syncw_branch_set_commit (SyncwBranch *branch, const char *commit_id);

void syncw_branch_ref (SyncwBranch *branch);
void syncw_branch_unref (SyncwBranch *branch);


typedef struct _SyncwBranchManager SyncwBranchManager;
typedef struct _SyncwBranchManagerPriv SyncwBranchManagerPriv;

struct _SyncwerkSession;
struct _SyncwBranchManager {
    struct _SyncwerkSession *syncw;

    SyncwBranchManagerPriv *priv;
};

SyncwBranchManager *syncw_branch_manager_new (struct _SyncwerkSession *syncw);
int syncw_branch_manager_init (SyncwBranchManager *mgr);

int
syncw_branch_manager_add_branch (SyncwBranchManager *mgr, SyncwBranch *branch);

int
syncw_branch_manager_del_branch (SyncwBranchManager *mgr,
                                const char *repo_id,
                                const char *name);

void
syncw_branch_list_free (GList *blist);

int
syncw_branch_manager_update_branch (SyncwBranchManager *mgr,
                                   SyncwBranch *branch);

#ifdef SYNCWERK_SERVER
/**
 * Atomically test whether the current head commit id on @branch
 * is the same as @old_commit_id and update branch in db.
 */
int
syncw_branch_manager_test_and_update_branch (SyncwBranchManager *mgr,
                                            SyncwBranch *branch,
                                            const char *old_commit_id);
#endif

SyncwBranch *
syncw_branch_manager_get_branch (SyncwBranchManager *mgr,
                                const char *repo_id,
                                const char *name);


gboolean
syncw_branch_manager_branch_exists (SyncwBranchManager *mgr,
                                   const char *repo_id,
                                   const char *name);

GList *
syncw_branch_manager_get_branch_list (SyncwBranchManager *mgr,
                                     const char *repo_id);

gint64
syncw_branch_manager_calculate_branch_size (SyncwBranchManager *mgr,
                                           const char *repo_id, 
                                           const char *commit_id);
#endif /* SYNCW_BRANCH_MGR_H */
