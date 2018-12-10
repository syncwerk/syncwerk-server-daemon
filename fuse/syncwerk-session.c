#include "common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ccnet.h>
#include <utils.h>
#include <locale.h>

#include "syncwerk-session.h"
#include "syncwerk-server-utils.h"

#include "log.h"

SyncwerkSession *
syncwerk_session_new(const char *central_config_dir,
                    const char *syncwerk_dir,
                    CcnetClient *ccnet_session)
{
    char *abs_central_config_dir = NULL;
    char *abs_syncwerk_dir;
    char *tmp_file_dir;
    char *config_file_path;
    struct stat st;
    GKeyFile *config;
    SyncwerkSession *session = NULL;

    if (!ccnet_session)
        return NULL;

    abs_syncwerk_dir = ccnet_expand_path (syncwerk_dir);
    tmp_file_dir = "/var/lib/syncwerk/session";
    if (central_config_dir) {
        abs_central_config_dir = ccnet_expand_path (central_config_dir);
    }
    config_file_path = g_build_filename(
        abs_central_config_dir ? abs_central_config_dir : abs_syncwerk_dir,
        "server.conf", NULL);

    if (g_stat(abs_syncwerk_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
        syncw_warning ("Syncwerk data dir %s does not exist and is unable to create\n",
                   abs_syncwerk_dir);
        goto onerror;
    }

    if (g_stat(tmp_file_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
        syncw_warning("Syncwerk tmp dir %s does not exist and is unable to create\n",
                  tmp_file_dir);
        goto onerror;
    }

    GError *error = NULL;
    config = g_key_file_new ();
    if (!g_key_file_load_from_file (config, config_file_path, 
                                    G_KEY_FILE_NONE, &error)) {
        syncw_warning ("Failed to load config file.\n");
        g_key_file_free (config);
        goto onerror;
    }

    session = g_new0(SyncwerkSession, 1);
    session->syncw_dir = abs_syncwerk_dir;
    session->tmp_file_dir = tmp_file_dir;
    session->session = ccnet_session;
    session->config = config;

    if (load_database_config (session) < 0) {
        syncw_warning ("Failed to load database config.\n");
        goto onerror;
    }

    session->fs_mgr = syncw_fs_manager_new (session, abs_syncwerk_dir);
    if (!session->fs_mgr)
        goto onerror;
    session->block_mgr = syncw_block_manager_new (session, abs_syncwerk_dir);
    if (!session->block_mgr)
        goto onerror;
    session->commit_mgr = syncw_commit_manager_new (session);
    if (!session->commit_mgr)
        goto onerror;
    session->repo_mgr = syncw_repo_manager_new (session);
    if (!session->repo_mgr)
        goto onerror;
    session->branch_mgr = syncw_branch_manager_new (session);
    if (!session->branch_mgr)
        goto onerror;

    return session;

onerror:
    free (abs_syncwerk_dir);
    g_free (config_file_path);
    g_free (session);
    return NULL;    
}

int
syncwerk_session_init (SyncwerkSession *session)
{
    if (syncw_commit_manager_init (session->commit_mgr) < 0)
        return -1;

    if (syncw_fs_manager_init (session->fs_mgr) < 0)
        return -1;

    if (syncw_branch_manager_init (session->branch_mgr) < 0)
        return -1;

    if (syncw_repo_manager_init (session->repo_mgr) < 0)
        return -1;

    return 0;
}

int
syncwerk_session_start (SyncwerkSession *session)
{
    return 0;
}
