#include "syncwerk-session.h"
#include "log.h"

typedef struct VerifyData {
    SyncwRepo *repo;
    gint64 truncate_time;
    gboolean traversed_head;
} VerifyData;

static int
check_blocks (VerifyData *data, const char *file_id)
{
    SyncwRepo *repo = data->repo;
    Syncwerk *syncwerk;
    int i;

    syncwerk = syncw_fs_manager_get_syncwerk (syncw->fs_mgr,
                                           repo->store_id,
                                           repo->version,
                                           file_id);
    if (!syncwerk) {
        syncw_warning ("Failed to find file %s.\n", file_id);
        return -1;
    }

    for (i = 0; i < syncwerk->n_blocks; ++i) {
        if (!syncw_block_manager_block_exists (syncw->block_mgr,
                                              repo->store_id,
                                              repo->version,
                                              syncwerk->blk_sha1s[i]))
            g_message ("Block %s is missing.\n", syncwerk->blk_sha1s[i]);
    }

    syncwerk_unref (syncwerk);

    return 0;
}

static gboolean
fs_callback (SyncwFSManager *mgr,
             const char *store_id,
             int version,
             const char *obj_id,
             int type,
             void *user_data,
             gboolean *stop)
{
    VerifyData *data = user_data;

    if (type == SYNCW_METADATA_TYPE_FILE && check_blocks (data, obj_id) < 0)
        return FALSE;

    return TRUE;
}

static gboolean
traverse_commit (SyncwCommit *commit, void *vdata, gboolean *stop)
{
    VerifyData *data = vdata;
    SyncwRepo *repo = data->repo;
    int ret;

    if (data->truncate_time == 0)
    {
        *stop = TRUE;
        /* Stop after traversing the head commit. */
    }
    else if (data->truncate_time > 0 &&
             (gint64)(commit->ctime) < data->truncate_time &&
             data->traversed_head)
    {
        *stop = TRUE;
        return TRUE;
    }

    if (!data->traversed_head)
        data->traversed_head = TRUE;

    ret = syncw_fs_manager_traverse_tree (syncw->fs_mgr,
                                         repo->store_id,
                                         repo->version,
                                         commit->root_id,
                                         fs_callback,
                                         vdata, FALSE);
    if (ret < 0)
        return FALSE;

    return TRUE;
}

static int
verify_repo (SyncwRepo *repo)
{
    GList *branches, *ptr;
    SyncwBranch *branch;
    int ret = 0;
    VerifyData data = {0};

    data.repo = repo;
    data.truncate_time = syncw_repo_manager_get_repo_truncate_time (repo->manager,
                                                                   repo->id);

    branches = syncw_branch_manager_get_branch_list (syncw->branch_mgr, repo->id);
    if (branches == NULL) {
        syncw_warning ("[GC] Failed to get branch list of repo %s.\n", repo->id);
        return -1;
    }

    for (ptr = branches; ptr != NULL; ptr = ptr->next) {
        branch = ptr->data;
        gboolean res = syncw_commit_manager_traverse_commit_tree (syncw->commit_mgr,
                                                                 repo->id,
                                                                 repo->version,
                                                                 branch->commit_id,
                                                                 traverse_commit,
                                                                 &data, FALSE);
        syncw_branch_unref (branch);
        if (!res) {
            ret = -1;
            break;
        }
    }

    g_list_free (branches);

    return ret;
}

int
verify_repos (GList *repo_id_list)
{
    if (repo_id_list == NULL)
        repo_id_list = syncw_repo_manager_get_repo_id_list (syncw->repo_mgr);

    GList *ptr;
    SyncwRepo *repo;
    int ret = 0;

    for (ptr = repo_id_list; ptr != NULL; ptr = ptr->next) {
        repo = syncw_repo_manager_get_repo_ex (syncw->repo_mgr, (const gchar *)ptr->data);

        g_free (ptr->data);

        if (!repo)
            continue;

        if (repo->is_corrupted) {
           syncw_warning ("Repo %s is corrupted.\n", repo->id);
        } else {
            ret = verify_repo (repo);
            syncw_repo_unref (repo);
            if (ret < 0)
                break;
        }
    }

    g_list_free (repo_id_list);

    return ret;
}
