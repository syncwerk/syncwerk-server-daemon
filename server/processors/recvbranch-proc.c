/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <ccnet.h>
#include <string.h>
#include <ccnet/ccnet-object.h>

#include "syncwerk-session.h"
#include "recvbranch-proc.h"
#include "vc-common.h"

#include "log.h"

#define SC_BAD_COMMIT   "401"
#define SS_BAD_COMMIT   "Commit does not exist"
#define SC_NOT_FF       "402"
#define SS_NOT_FF       "Not fast forward"
#define SC_QUOTA_ERROR  "403"
#define SS_QUOTA_ERROR  "Failed to get quota"
#define SC_QUOTA_FULL   "404"
#define SS_QUOTA_FULL   "storage for the repo's owner is full"
#define SC_SERVER_ERROR "405"
#define SS_SERVER_ERROR "Internal server error"
#define SC_BAD_REPO     "406"
#define SS_BAD_REPO     "Repo does not exist"
#define SC_BAD_BRANCH   "407"
#define SS_BAD_BRANCH   "Branch does not exist"
#define SC_ACCESS_DENIED "410"
#define SS_ACCESS_DENIED "Access denied"

typedef struct {
    char repo_id[37];
    char *branch_name;
    char new_head[41];
    char *email;

    char *rsp_code;
    char *rsp_msg;
} SyncwerkRecvbranchProcPriv;

G_DEFINE_TYPE (SyncwerkRecvbranchProc, syncwerk_recvbranch_proc, CCNET_TYPE_PROCESSOR)

#define GET_PRIV(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), SYNCWERK_TYPE_RECVBRANCH_PROC, SyncwerkRecvbranchProcPriv))

#define USE_PRIV \
    SyncwerkRecvbranchProcPriv *priv = GET_PRIV(processor);

static int start (CcnetProcessor *processor, int argc, char **argv);
static void handle_update (CcnetProcessor *processor,
                           char *code, char *code_msg,
                           char *content, int clen);
static void *update_repo (void *vprocessor);
static void thread_done (void *result);

static void
release_resource(CcnetProcessor *processor)
{
    USE_PRIV;
    g_free (priv->branch_name);
    g_free (priv->rsp_code);
    g_free (priv->rsp_msg);

    CCNET_PROCESSOR_CLASS (syncwerk_recvbranch_proc_parent_class)->release_resource (processor);
}


static void
syncwerk_recvbranch_proc_class_init (SyncwerkRecvbranchProcClass *klass)
{
    CcnetProcessorClass *proc_class = CCNET_PROCESSOR_CLASS (klass);

    proc_class->name = "recvbranch-proc";
    proc_class->start = start;
    proc_class->handle_update = handle_update;
    proc_class->release_resource = release_resource;

    g_type_class_add_private (klass, sizeof (SyncwerkRecvbranchProcPriv));
}

static void
syncwerk_recvbranch_proc_init (SyncwerkRecvbranchProc *processor)
{
}

static int
start (CcnetProcessor *processor, int argc, char **argv)
{
    USE_PRIV;
    char *session_token;

    if (argc != 4) {
        ccnet_processor_send_response (processor, SC_BAD_ARGS, SS_BAD_ARGS, NULL, 0);
        ccnet_processor_done (processor, FALSE);
        return -1;
    }

    if (!is_uuid_valid(argv[0]) || strlen(argv[2]) != 40) {
        ccnet_processor_send_response (processor, SC_BAD_ARGS, SS_BAD_ARGS, NULL, 0);
        ccnet_processor_done (processor, FALSE);
        return -1;
    }
    memcpy (priv->repo_id, argv[0], 36);
    memcpy (priv->new_head, argv[2], 40);
    priv->branch_name = g_strdup(argv[1]);
    session_token = argv[3];

    if (syncw_token_manager_verify_token (syncw->token_mgr,
                                         NULL,
                                         processor->peer_id,
                                         session_token, NULL) < 0) {
        ccnet_processor_send_response (processor, 
                                       SC_ACCESS_DENIED, SS_ACCESS_DENIED,
                                       NULL, 0);
        ccnet_processor_done (processor, FALSE);
        return -1;
    }

    ccnet_processor_thread_create (processor,
                                   syncw->job_mgr,
                                   update_repo,
                                   thread_done,
                                   processor);

    return 0;
}


static void
handle_update (CcnetProcessor *processor,
               char *code, char *code_msg,
               char *content, int clen)
{
}

static void *
update_repo (void *vprocessor)
{
    CcnetProcessor *processor = vprocessor;
    USE_PRIV;
    char *repo_id, *new_head;
    SyncwRepo *repo = NULL;
    SyncwBranch *branch = NULL;
    SyncwCommit *commit = NULL;
    char old_commit_id[41];

    repo_id = priv->repo_id;
    new_head = priv->new_head;

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        /* repo is deleted on server */
        priv->rsp_code = g_strdup (SC_BAD_REPO);
        priv->rsp_msg = g_strdup (SC_BAD_REPO);
        goto out;

    }

    /* Since this is the last step of upload procedure, commit should exist. */
    commit = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                             repo->id, repo->version,
                                             new_head);
    if (!commit) {
        priv->rsp_code = g_strdup (SC_BAD_COMMIT);
        priv->rsp_msg = g_strdup (SS_BAD_COMMIT);
        goto out;
    }

    if (syncw_quota_manager_check_quota (syncw->quota_mgr, repo_id) < 0) {
        priv->rsp_code = g_strdup(SC_QUOTA_FULL);
        priv->rsp_msg = g_strdup(SS_QUOTA_FULL);
        goto out;
    }

    branch = syncw_branch_manager_get_branch (syncw->branch_mgr, repo_id, "master");
    if (!branch) {
        priv->rsp_code = g_strdup (SC_BAD_BRANCH);
        priv->rsp_msg = g_strdup (SS_BAD_BRANCH);
        goto out;
    }

    /* If branch exists, check fast forward. */
    if (strcmp (new_head, branch->commit_id) != 0 &&
        !is_fast_forward (repo->id, repo->version, new_head, branch->commit_id)) {
        syncw_warning ("Upload is not fast forward. Refusing.\n");

        syncw_repo_unref (repo);
        syncw_commit_unref (commit);
        syncw_branch_unref (branch);

        priv->rsp_code = g_strdup (SC_NOT_FF);
        priv->rsp_msg = g_strdup (SS_NOT_FF);
        return vprocessor;
    }

    /* Update branch. In case of concurrent update, we must ensure atomicity.
     */
    memcpy (old_commit_id, branch->commit_id, 41);
    syncw_branch_set_commit (branch, commit->commit_id);
    if (syncw_branch_manager_test_and_update_branch (syncw->branch_mgr,
                                                    branch, old_commit_id) < 0)
    {
        priv->rsp_code = g_strdup (SC_NOT_FF);
        priv->rsp_msg = g_strdup (SS_NOT_FF);
        goto out;
    }

    syncw_repo_manager_cleanup_virtual_repos (syncw->repo_mgr, repo_id);
    syncw_repo_manager_merge_virtual_repo (syncw->repo_mgr, repo_id, NULL);

out:
    if (repo)   syncw_repo_unref (repo);
    if (commit) syncw_commit_unref (commit);
    if (branch) syncw_branch_unref (branch);

    if (!priv->rsp_code) {
        priv->rsp_code = g_strdup (SC_OK);
        priv->rsp_msg = g_strdup (SS_OK);
    }

    return vprocessor;
}

static void 
thread_done (void *result)
{
    CcnetProcessor *processor = result;
    USE_PRIV;

    if (strcmp (priv->rsp_code, SC_OK) == 0) {
        /* Repo is updated, schedule repo size computation. */
        schedule_repo_size_computation (syncw->size_sched, priv->repo_id);

        ccnet_processor_send_response (processor, SC_OK, SS_OK, NULL, 0);
        ccnet_processor_done (processor, TRUE);
    } else {
        ccnet_processor_send_response (processor,
                                       priv->rsp_code, priv->rsp_msg,
                                       NULL, 0);
        ccnet_processor_done (processor, FALSE);
    }
}

