#include "common.h"

#include <unistd.h>
#include <getopt.h>

#define FUSE_USE_VERSION  26
#include <fuse.h>
#include <fuse_opt.h>

#include <glib.h>
#include <glib-object.h>

#include <ccnet.h>
#include <syncwerk-server-db.h>

#include "log.h"
#include "utils.h"

#include "syncwerk-server-fuse.h"

CcnetClient *ccnet_client = NULL;
SyncwerkSession *syncw = NULL;

static char *parse_repo_id (const char *repo_id_name)
{
    if (strlen(repo_id_name) < 36)
        return NULL;
    return g_strndup(repo_id_name, 36);
}

/*
 * Path format can be:
 * 1. / --> list all users
 * 2. /user --> list libraries owned by user
 * 3. /user/repo-id_name --> list root of the library
 * 4. /user/repo-id_name/repo_path --> list library content
 */
int parse_fuse_path (const char *path,
                     int *n_parts, char **user, char **repo_id, char **repo_path)
{
    char **tokens;
    int n;
    int ret = 0;

    *user = NULL;
    *repo_id = NULL;
    *repo_path = NULL;

    if (*path == '/')
        ++path;

    tokens = g_strsplit (path, "/", 3);
    n = g_strv_length (tokens);
    *n_parts = n;

    switch (n) {
    case 0:
        break;
    case 1:
        *user = g_strdup(tokens[0]);
        break;
    case 2:
        *repo_id = parse_repo_id(tokens[1]);
        if (*repo_id == NULL) {
            ret = -1;
            break;
        }
        *user = g_strdup(tokens[0]);
        *repo_path = g_strdup("/");
        break;
    case 3:
        *repo_id = parse_repo_id(tokens[1]);
        if (*repo_id == NULL) {
            ret = -1;
            break;
        }
        *user = g_strdup(tokens[0]);
        *repo_path = g_strdup(tokens[2]);
        break;
    }

    g_strfreev (tokens);
    return ret;
}

static int syncwerk_server_fuse_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    return do_getattr(syncw, path, stbuf);
}

static int syncwerk_server_fuse_readdir(const char *path, void *buf,
                             fuse_fill_dir_t filler, off_t offset,
                             struct fuse_file_info *info)
{
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    return do_readdir(syncw, path, buf, filler, offset, info);
}

static int syncwerk_server_fuse_open(const char *path, struct fuse_file_info *info)
{
    int n_parts;
    char *user, *repo_id, *repo_path;
    SyncwRepo *repo = NULL;
    SyncwBranch *branch = NULL;
    SyncwCommit *commit = NULL;
    guint32 mode = 0;
    int ret = 0;

    /* Now we only support read-only mode */
    if ((info->flags & 3) != O_RDONLY)
        return -EACCES;

    if (parse_fuse_path (path, &n_parts, &user, &repo_id, &repo_path) < 0) {
        syncw_warning ("Invalid input path %s.\n", path);
        return -ENOENT;
    }

    if (n_parts != 2 && n_parts != 3) {
        syncw_warning ("Invalid input path for open: %s.\n", path);
        ret = -EACCES;
        goto out;
    }

    repo = syncw_repo_manager_get_repo(syncw->repo_mgr, repo_id);
    if (!repo) {
        syncw_warning ("Failed to get repo %s.\n", repo_id);
        ret = -ENOENT;
        goto out;
    }

    branch = repo->head;
    commit = syncw_commit_manager_get_commit(syncw->commit_mgr,
                                            repo->id,
                                            repo->version,
                                            branch->commit_id);
    if (!commit) {
        syncw_warning ("Failed to get commit %s:%.8s.\n", repo->id, branch->commit_id);
        ret = -ENOENT;
        goto out;
    }

    char *id = syncw_fs_manager_path_to_obj_id(syncw->fs_mgr,
                                              repo->store_id, repo->version,
                                              commit->root_id,
                                              repo_path, &mode, NULL);
    if (!id) {
        syncw_warning ("Path %s doesn't exist in repo %s.\n", repo_path, repo_id);
        ret = -ENOENT;
        goto out;
    }
    g_free (id);

    if (!S_ISREG(mode))
        return -EACCES;

out:
    g_free (user);
    g_free (repo_id);
    g_free (repo_path);
    syncw_repo_unref (repo);
    syncw_commit_unref (commit);
    return ret;
}

static int syncwerk_server_fuse_read(const char *path, char *buf, size_t size,
                          off_t offset, struct fuse_file_info *info)
{
    int n_parts;
    char *user, *repo_id, *repo_path;
    SyncwRepo *repo = NULL;
    SyncwBranch *branch = NULL;
    SyncwCommit *commit = NULL;
    Syncwerk *file = NULL;
    char *file_id = NULL;
    int ret = 0;

    /* Now we only support read-only mode */
    if ((info->flags & 3) != O_RDONLY)
        return -EACCES;

    if (parse_fuse_path (path, &n_parts, &user, &repo_id, &repo_path) < 0) {
        syncw_warning ("Invalid input path %s.\n", path);
        return -ENOENT;
    }

    if (n_parts != 2 && n_parts != 3) {
        syncw_warning ("Invalid input path for open: %s.\n", path);
        ret = -EACCES;
        goto out;
    }

    repo = syncw_repo_manager_get_repo(syncw->repo_mgr, repo_id);
    if (!repo) {
        syncw_warning ("Failed to get repo %s.\n", repo_id);
        ret = -ENOENT;
        goto out;
    }

    branch = repo->head;
    commit = syncw_commit_manager_get_commit(syncw->commit_mgr,
                                            repo->id,
                                            repo->version,
                                            branch->commit_id);
    if (!commit) {
        syncw_warning ("Failed to get commit %s:%.8s.\n", repo->id, branch->commit_id);
        ret = -ENOENT;
        goto out;
    }

    file_id = syncw_fs_manager_get_syncwerk_id_by_path(syncw->fs_mgr,
                                                     repo->store_id, repo->version,
                                                     commit->root_id,
                                                     repo_path, NULL);
    if (!file_id) {
        syncw_warning ("Path %s doesn't exist in repo %s.\n", repo_path, repo_id);
        ret = -ENOENT;
        goto out;
    }

    file = syncw_fs_manager_get_syncwerk(syncw->fs_mgr,
                                       repo->store_id, repo->version, file_id);
    if (!file) {
        ret = -ENOENT;
        goto out;
    }

    ret = read_file(syncw, repo->store_id, repo->version,
                    file, buf, size, offset, info);
    syncwerk_unref (file);

out:
    g_free (user);
    g_free (repo_id);
    g_free (repo_path);
    g_free (file_id);
    syncw_repo_unref (repo);
    syncw_commit_unref (commit);
    return ret;
}

struct options {
    char *central_config_dir;
    char *config_dir;
    char *syncwerk_dir;
    char *log_file;
} options;

#define SYNCW_FUSE_OPT_KEY(t, p, v) { t, offsetof(struct options, p), v }

enum {
    KEY_VERSION,
    KEY_HELP,
};

static struct fuse_opt syncwerk_server_fuse_opts[] = {
    SYNCW_FUSE_OPT_KEY("-c %s", config_dir, 0),
    SYNCW_FUSE_OPT_KEY("--config %s", config_dir, 0),
    SYNCW_FUSE_OPT_KEY("-F %s", central_config_dir, 0),
    SYNCW_FUSE_OPT_KEY("--central-config-dir %s", central_config_dir, 0),
    SYNCW_FUSE_OPT_KEY("-d %s", syncwerk_dir, 0),
    SYNCW_FUSE_OPT_KEY("--syncwdir %s", syncwerk_dir, 0),
    SYNCW_FUSE_OPT_KEY("-l %s", log_file, 0),
    SYNCW_FUSE_OPT_KEY("--logfile %s", log_file, 0),

    FUSE_OPT_KEY("-V", KEY_VERSION),
    FUSE_OPT_KEY("--version", KEY_VERSION),
    FUSE_OPT_KEY("-h", KEY_HELP),
    FUSE_OPT_KEY("--help", KEY_HELP),
    FUSE_OPT_END
};

static struct fuse_operations syncwerk_server_fuse_ops = {
    .getattr = syncwerk_server_fuse_getattr,
    .readdir = syncwerk_server_fuse_readdir,
    .open    = syncwerk_server_fuse_open,
    .read    = syncwerk_server_fuse_read,
};

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    const char *debug_str = NULL;
    char *config_dir = DEFAULT_CONFIG_DIR;
    char *central_config_dir = NULL;
    char *syncwerk_dir = NULL;
    char *logfile = NULL;
    char *ccnet_debug_level_str = "info";
    char *syncwerk_debug_level_str = "debug";
    int ret;

    memset(&options, 0, sizeof(struct options));

    if (fuse_opt_parse(&args, &options, syncwerk_server_fuse_opts, NULL) == -1) {
        syncw_warning("Parse argument Failed\n");
        exit(1);
    }

    g_type_init();

    config_dir = options.config_dir ? : DEFAULT_CONFIG_DIR;
    config_dir = ccnet_expand_path (config_dir);
    central_config_dir = options.central_config_dir;

    if (!debug_str)
        debug_str = g_getenv("SYNCWERK_DEBUG");
    syncwerk_debug_set_flags_string(debug_str);

    if (!options.syncwerk_dir)
        syncwerk_dir = g_build_filename(config_dir, "syncwerk", NULL);
    else
        syncwerk_dir = options.syncwerk_dir;

    if (!options.log_file)
        logfile = g_build_filename(syncwerk_dir, "syncwerk-server-fuse.log", NULL);
    else
        logfile = options.log_file;

    if (syncwerk_log_init(logfile, ccnet_debug_level_str,
                         syncwerk_debug_level_str) < 0) {
        fprintf(stderr, "Failed to init log.\n");
        exit(1);
    }

    ccnet_client = ccnet_client_new();
    if ((ccnet_client_load_confdir(ccnet_client, central_config_dir, config_dir)) < 0) {
        syncw_warning("Read config dir error\n");
        exit(1);
    }

    syncw = syncwerk_session_new(central_config_dir, syncwerk_dir, ccnet_client);
    if (!syncw) {
        syncw_warning("Failed to create syncwerk session.\n");
        exit(1);
    }

    if (syncwerk_session_init(syncw) < 0) {
        syncw_warning("Failed to init syncwerk session.\n");
        exit(1);
    }

    syncw->client_pool = ccnet_client_pool_new(central_config_dir, config_dir);
    if (!syncw->client_pool) {
        syncw_warning("Failed to creat client pool\n");
        exit(1);
    }

    set_syslog_config (syncw->config);

    ret = fuse_main(args.argc, args.argv, &syncwerk_server_fuse_ops, NULL);
    fuse_opt_free_args(&args);
    return ret;
}
