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

static char *replace_slash (const char *repo_name)
{
    char *ret = g_strdup(repo_name);
    char *p;

    for (p = ret; *p != 0; ++p)
        if (*p == '/')
            *p = '_';

    return ret;
}

static GList *get_users_from_ccnet (RpcsyncwerkClient *client, const char *source)
{
    return rpcsyncwerk_client_call__objlist (client,
                                        "get_emailusers", CCNET_TYPE_EMAIL_USER, NULL,
                                        3, "string", source, "int", -1, "int", -1);
}

static CcnetEmailUser *get_user_from_ccnet (RpcsyncwerkClient *client, const char *user)
{
    return (CcnetEmailUser *)rpcsyncwerk_client_call__object (client,
                                       "get_emailuser", CCNET_TYPE_EMAIL_USER, NULL,
                                       1, "string", user);
}

static int readdir_root(SyncwerkSession *syncw,
                        void *buf, fuse_fill_dir_t filler, off_t offset,
                        struct fuse_file_info *info)
{
    RpcsyncwerkClient *client = NULL;
    GList *users, *p;
    CcnetEmailUser *user;
    const char *email;
    GHashTable *user_hash;
    int dummy;

    client = ccnet_create_pooled_rpc_client (syncw->client_pool,
                                             NULL,
                                             "ccnet-threaded-rpcserver");
    if (!client) {
        syncw_warning ("Failed to alloc rpc client.\n");
        return -ENOMEM;
    }

    user_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    users = get_users_from_ccnet (client, "DB");
    for (p = users; p; p = p->next) {
        user = p->data;
        email = ccnet_email_user_get_email (user);
        g_hash_table_insert (user_hash, g_strdup(email), &dummy);
        g_object_unref (user);
    }
    g_list_free (users);

    users = get_users_from_ccnet (client, "LDAP");
    for (p = users; p; p = p->next) {
        user = p->data;
        email = ccnet_email_user_get_email (user);
        g_hash_table_insert (user_hash, g_strdup(email), &dummy);
        g_object_unref (user);
    }
    g_list_free (users);

    users = g_hash_table_get_keys (user_hash);
    for (p = users; p; p = p->next) {
        email = p->data;
        filler (buf, email, NULL, 0);
    }
    g_list_free (users);

    g_hash_table_destroy (user_hash);
    ccnet_rpc_client_free (client);

    return 0;
}

static int readdir_user(SyncwerkSession *syncw, const char *user,
                        void *buf, fuse_fill_dir_t filler, off_t offset,
                        struct fuse_file_info *info)
{
    RpcsyncwerkClient *client;
    CcnetEmailUser *emailuser;
    GList *list = NULL, *p;
    GString *name;

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

    list = syncw_repo_manager_get_repos_by_owner (syncw->repo_mgr, user);
    if (!list)
        return 0;

    for (p = list; p; p = p->next) {
        SyncwRepo *repo = (SyncwRepo *)p->data;

        /* Don't list virtual repos. */
        if (syncw_repo_manager_is_virtual_repo(syncw->repo_mgr, repo->id)) {
            syncw_repo_unref (repo);
            continue;
        }

        // Don't list encrypted repo
        if (repo->encrypted) {
            continue;
        }

        char *clean_repo_name = replace_slash (repo->name);

        name = g_string_new ("");
        g_string_printf (name, "%s_%s", repo->id, clean_repo_name);
        filler(buf, name->str, NULL, 0);
        g_string_free (name, TRUE);
        g_free (clean_repo_name);

        syncw_repo_unref (repo);
    }

    g_list_free (list);

    return 0;
}

static int readdir_repo(SyncwerkSession *syncw,
                        const char *user, const char *repo_id, const char *repo_path,
                        void *buf, fuse_fill_dir_t filler, off_t offset,
                        struct fuse_file_info *info)
{
    SyncwRepo *repo = NULL;
    SyncwBranch *branch;
    SyncwCommit *commit = NULL;
    SyncwDir *dir = NULL;
    GList *l;
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

    dir = syncw_fs_manager_get_syncwdir_by_path(syncw->fs_mgr,
                                              repo->store_id, repo->version,
                                              commit->root_id,
                                              repo_path, NULL);
    if (!dir) {
        syncw_warning ("Path %s doesn't exist in repo %s.\n", repo_path, repo_id);
        ret = -ENOENT;
        goto out;
    }

    for (l = dir->entries; l; l = l->next) {
        SyncwDirent *syncw_dent = (SyncwDirent *) l->data;
        /* FIXME: maybe we need to return stbuf */
        filler(buf, syncw_dent->name, NULL, 0);
    }

out:
    syncw_repo_unref (repo);
    syncw_commit_unref (commit);
    syncw_dir_free (dir);
    return ret;
}

int do_readdir(SyncwerkSession *syncw, const char *path, void *buf,
               fuse_fill_dir_t filler, off_t offset,
               struct fuse_file_info *info)
{
    int n_parts;
    char *user, *repo_id, *repo_path;
    int ret = 0;

    if (parse_fuse_path (path, &n_parts, &user, &repo_id, &repo_path) < 0) {
        return -ENOENT;
    }

    switch (n_parts) {
    case 0:
        ret = readdir_root(syncw, buf, filler, offset, info);
        break;
    case 1:
        ret = readdir_user(syncw, user, buf, filler, offset, info);
        break;
    case 2:
    case 3:
        ret = readdir_repo(syncw, user, repo_id, repo_path, buf, filler, offset, info);
        break;
    }

    g_free (user);
    g_free (repo_id);
    g_free (repo_path);
    return ret;
}
