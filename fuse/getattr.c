#include "common.h"

#define FUSE_USE_VERSION  26
#include <fuse.h>

#include <glib.h>
#include <glib-object.h>

#include <ccnet.h>
#include <ccnet/ccnet-object.h>
#include <syncwerk-server-db.h>

#include "log.h"
#include "utils.h"

#include "syncwerk-server-fuse.h"
#include "syncwerk-session.h"

static CcnetEmailUser *get_user_from_ccnet (RpcsyncwerkClient *client, const char *user)
{
    return (CcnetEmailUser *)rpcsyncwerk_client_call__object (client,
                                       "get_emailuser", CCNET_TYPE_EMAIL_USER, NULL,
                                       1, "string", user);
}

static int getattr_root(SyncwerkSession *syncw, struct stat *stbuf)
{
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    stbuf->st_size = 4096;

    return 0;
}

static int getattr_user(SyncwerkSession *syncw, const char *user, struct stat *stbuf)
{
    RpcsyncwerkClient *client;
    CcnetEmailUser *emailuser;

    client = ccnet_create_pooled_rpc_client (syncw->client_pool,
                                             NULL,
                                             "ccnet-threaded-rpcserver");
    if (!client) {
        syncw_warning ("Failed to alloc rpc client.\n");
        return -ENOMEM;
    }

    emailuser = get_user_from_ccnet (client, user);
    if (!emailuser) {
        ccnet_rpc_client_free (client);
        return -ENOENT;
    }
    g_object_unref (emailuser);
    ccnet_rpc_client_free (client);

    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    stbuf->st_size = 4096;

    return 0;
}

static int getattr_repo(SyncwerkSession *syncw,
                        const char *user, const char *repo_id, const char *repo_path,
                        struct stat *stbuf)
{
    SyncwRepo *repo = NULL;
    SyncwBranch *branch;
    SyncwCommit *commit = NULL;
    guint32 mode = 0;
    char *id = NULL;
    int ret = 0;

    repo = syncw_repo_manager_get_repo(syncw->repo_mgr, repo_id);
    if (!repo) {
        syncw_warning ("Failed to get repo %s.\n", repo_id);
        ret = -ENOENT;
        goto out;
    }

    branch = repo->head;
    commit = syncw_commit_manager_get_commit(syncw->commit_mgr,
                                            repo->id, repo->version,
                                            branch->commit_id);
    if (!commit) {
        syncw_warning ("Failed to get commit %s:%.8s.\n", repo->id, branch->commit_id);
        ret = -ENOENT;
        goto out;
    }

    id = syncw_fs_manager_path_to_obj_id(syncw->fs_mgr,
                                        repo->store_id, repo->version,
                                        commit->root_id,
                                        repo_path, &mode, NULL);
    if (!id) {
        syncw_warning ("Path %s doesn't exist in repo %s.\n", repo_path, repo_id);
        ret = -ENOENT;
        goto out;
    }

    if (S_ISDIR(mode)) {
        SyncwDir *dir;
        GList *l;
        int cnt = 2; /* '.' and '..' */

        dir = syncw_fs_manager_get_syncwdir(syncw->fs_mgr,
                                          repo->store_id, repo->version, id);
        if (dir) {
            for (l = dir->entries; l; l = l->next)
                cnt++;
        }

        if (strcmp (repo_path, "/") != 0) {
            // get dirent of the dir
            SyncwDirent *dirent = syncw_fs_manager_get_dirent_by_path (syncw->fs_mgr,
                                                                     repo->store_id,
                                                                     repo->version,
                                                                     commit->root_id,
                                                                     repo_path, NULL);
            if (dirent && repo->version != 0)
                stbuf->st_mtime = dirent->mtime;

            syncw_dirent_free (dirent);
        }

        stbuf->st_size += cnt * sizeof(SyncwDirent);
        stbuf->st_mode = mode | 0755;
        stbuf->st_nlink = 2;

        syncw_dir_free (dir);
    } else if (S_ISREG(mode)) {
        Syncwerk *file;

        file = syncw_fs_manager_get_syncwerk(syncw->fs_mgr,
                                           repo->store_id, repo->version, id);
        if (file)
            stbuf->st_size = file->file_size;

        SyncwDirent *dirent = syncw_fs_manager_get_dirent_by_path (syncw->fs_mgr,
                                                                 repo->store_id,
                                                                 repo->version,
                                                                 commit->root_id,
                                                                 repo_path, NULL);
        if (dirent && repo->version != 0)
            stbuf->st_mtime = dirent->mtime;

        stbuf->st_mode = mode | 0644;
        stbuf->st_nlink = 1;

        syncw_dirent_free (dirent);
        syncwerk_unref (file);
    } else {
        return -ENOENT;
    }

out:
    g_free (id);
    syncw_repo_unref (repo);
    syncw_commit_unref (commit);
    return ret;
}

int do_getattr(SyncwerkSession *syncw, const char *path, struct stat *stbuf)
{
    int n_parts;
    char *user, *repo_id, *repo_path;
    int ret = 0;

    if (parse_fuse_path (path, &n_parts, &user, &repo_id, &repo_path) < 0) {
        return -ENOENT;
    }

    switch (n_parts) {
    case 0:
        ret = getattr_root(syncw, stbuf);
        break;
    case 1:
        ret = getattr_user(syncw, user, stbuf);
        break;
    case 2:
    case 3:
        ret = getattr_repo(syncw, user, repo_id, repo_path, stbuf);
        break;
    }

    g_free (user);
    g_free (repo_id);
    g_free (repo_path);
    return ret;
}
