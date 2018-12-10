/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"
#include <glib/gstdio.h>
#include <ctype.h>

#include <sys/stat.h>
#include <dirent.h>
#include <ccnet.h>
#include "utils.h"

#include "syncwerk-session.h"
#include "fs-mgr.h"
#include "repo-mgr.h"
#include "syncwerk-error.h"
#include "syncwerk-rpc.h"

#ifdef SYNCWERK_SERVER
#include "web-accesstoken-mgr.h"
#endif

#ifndef SYNCWERK_SERVER
#include "syncwerk-config.h"
#endif

#define DEBUG_FLAG SYNCWERK_DEBUG_OTHER
#include "log.h"

#ifndef SYNCWERK_SERVER
#include "../daemon/vc-utils.h"

#endif  /* SYNCWERK_SERVER */


/* -------- Utilities -------- */
static GObject*
convert_repo (SyncwRepo *r)
{
    SyncwerkRepo *repo = NULL;

#ifndef SYNCWERK_SERVER
    if (r->head == NULL)
        return NULL;

    if (r->worktree_invalid && !syncwerk_session_config_get_allow_invalid_worktree(syncw))
        return NULL;
#endif

    repo = syncwerk_repo_new ();
    if (!repo)
        return NULL;

    g_object_set (repo, "id", r->id, "name", r->name,
                  "desc", r->desc, "encrypted", r->encrypted,
                  "magic", r->magic, "enc_version", r->enc_version,
                  "head_cmmt_id", r->head ? r->head->commit_id : NULL,
                  "root", r->root_id,
                  "version", r->version, "last_modify", r->last_modify,
                  "last_modifier", r->last_modifier,
                  NULL);
    g_object_set (repo,
                  "repo_id", r->id, "repo_name", r->name,
                  "repo_desc", r->desc, "last_modified", r->last_modify,
                  NULL);

#ifdef SYNCWERK_SERVER
    if (r->virtual_info) {
        g_object_set (repo,
                      "is_virtual", TRUE,
                      "origin_repo_id", r->virtual_info->origin_repo_id,
                      "origin_path", r->virtual_info->path,
                      NULL);
    }

    if (r->encrypted && r->enc_version == 2)
        g_object_set (repo, "random_key", r->random_key, NULL);

    g_object_set (repo, "store_id", r->store_id,
                  "repaired", r->repaired,
                  "size", r->size, "file_count", r->file_count, NULL);
    g_object_set (repo, "is_corrupted", r->is_corrupted, NULL);
#endif

#ifndef SYNCWERK_SERVER
    g_object_set (repo, "worktree", r->worktree,
                  "relay-id", r->relay_id,
                  "worktree-invalid", r->worktree_invalid,
                  "last-sync-time", r->last_sync_time,
                  "auto-sync", r->auto_sync,
                  NULL);

#endif  /* SYNCWERK_SERVER */

    return (GObject *)repo;
}

static void
free_repo_obj (gpointer repo)
{
    if (!repo)
        return;
    g_object_unref ((GObject *)repo);
}

static GList *
convert_repo_list (GList *inner_repos)
{
    GList *ret = NULL, *ptr;
    GObject *repo = NULL;

    for (ptr = inner_repos; ptr; ptr=ptr->next) {
        SyncwRepo *r = ptr->data;
        repo = convert_repo (r);
        if (!repo) {
            g_list_free_full (ret, free_repo_obj);
            return NULL;
        }

        ret = g_list_prepend (ret, repo);
    }

    return g_list_reverse (ret);
}

/*
 * RPC functions only available for clients.
 */

#ifndef SYNCWERK_SERVER

#include "sync-mgr.h"

GObject *
syncwerk_get_session_info (GError **error)
{
    SyncwerkSessionInfo *info;

    info = syncwerk_session_info_new ();
    g_object_set (info, "datadir", syncw->syncw_dir, NULL);
    return (GObject *) info;
}

int
syncwerk_set_config (const char *key, const char *value, GError **error)
{
    return syncwerk_session_config_set_string(syncw, key, value);
}

char *
syncwerk_get_config (const char *key, GError **error)
{
    return syncwerk_session_config_get_string(syncw, key);
}

int
syncwerk_set_config_int (const char *key, int value, GError **error)
{
    return syncwerk_session_config_set_int(syncw, key, value);
}

int
syncwerk_get_config_int (const char *key, GError **error)
{
    gboolean exists = TRUE;

    int ret = syncwerk_session_config_get_int(syncw, key, &exists);

    if (!exists) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Config not exists");
        return -1;
    }

    return ret;
}

int
syncwerk_set_upload_rate_limit (int limit, GError **error)
{
    if (limit < 0)
        limit = 0;

    syncw->sync_mgr->upload_limit = limit;

    return syncwerk_session_config_set_int (syncw, KEY_UPLOAD_LIMIT, limit);
}

int
syncwerk_set_download_rate_limit (int limit, GError **error)
{
    if (limit < 0)
        limit = 0;

    syncw->sync_mgr->download_limit = limit;

    return syncwerk_session_config_set_int (syncw, KEY_DOWNLOAD_LIMIT, limit);
}

int
syncwerk_repo_last_modify(const char *repo_id, GError **error)
{
    SyncwRepo *repo;
    int ctime = 0;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_REPO, "No such repository");
        return -1;
    }

    ctime = repo->last_modify;
#ifdef SYNCWERK_SERVER
    syncw_repo_unref (repo);
#endif

    return ctime;
}

GObject *
syncwerk_get_checkout_task (const char *repo_id, GError **error)
{
    if (!repo_id) {
        syncw_warning ("Invalid args\n");
        return NULL;
    }

    CheckoutTask *task;
    task = syncw_repo_manager_get_checkout_task(syncw->repo_mgr,
                                               repo_id);
    if (!task)
        return NULL;

    SyncwerkCheckoutTask *c_task = g_object_new
        (SYNCWERK_TYPE_CHECKOUT_TASK,
         "repo_id", task->repo_id,
         "worktree", task->worktree,
         "total_files", task->total_files,
         "finished_files", task->finished_files,
         NULL);

    return (GObject *)c_task;
}

char *
syncwerk_gen_default_worktree (const char *worktree_parent,
                              const char *repo_name,
                              GError **error)
{
    if (!worktree_parent || !repo_name) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Empty args");
        return NULL;
    }

    return syncw_clone_manager_gen_default_worktree (syncw->clone_mgr,
                                                    worktree_parent,
                                                    repo_name);
}

int
syncwerk_check_path_for_clone (const char *path, GError **error)
{
    if (!syncw_clone_manager_check_worktree_path(syncw->clone_mgr, path, error)) {
        return -1;
    }

    return 0;
}

char *
syncwerk_clone (const char *repo_id,
               int repo_version,
               const char *relay_id,
               const char *repo_name,
               const char *worktree,
               const char *token,
               const char *passwd,
               const char *magic,
               const char *peer_addr,
               const char *peer_port,
               const char *email,
               const char *random_key,
               int enc_version,
               const char *more_info,
               GError **error)
{
    if (!repo_id || strlen(repo_id) != 36) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    if (!relay_id || strlen(relay_id) != 40) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid peer id");
        return NULL;
    }

    if (!worktree) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Worktre must be specified");
        return NULL;
    }

    if (!token || !peer_addr || !peer_port || !email ) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Argument can't be NULL");
        return NULL;
    }

    return syncw_clone_manager_add_task (syncw->clone_mgr,
                                        repo_id, repo_version,
                                        relay_id,
                                        repo_name, token,
                                        passwd, magic,
                                        enc_version, random_key,
                                        worktree,
                                        peer_addr, peer_port,
                                        email, more_info,
                                        error);
}

char *
syncwerk_download (const char *repo_id,
                  int repo_version,
                  const char *relay_id,
                  const char *repo_name,
                  const char *wt_parent,
                  const char *token,
                  const char *passwd,
                  const char *magic,
                  const char *peer_addr,
                  const char *peer_port,
                  const char *email,
                  const char *random_key,
                  int enc_version,
                  const char *more_info,
                  GError **error)
{
    if (!repo_id || strlen(repo_id) != 36) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    if (!relay_id || strlen(relay_id) != 40) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid peer id");
        return NULL;
    }

    if (!wt_parent) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Worktre must be specified");
        return NULL;
    }

    if (!token || !peer_addr || !peer_port || !email ) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Argument can't be NULL");
        return NULL;
    }

    return syncw_clone_manager_add_download_task (syncw->clone_mgr,
                                                 repo_id, repo_version,
                                                 relay_id,
                                                 repo_name, token,
                                                 passwd, magic,
                                                 enc_version, random_key,
                                                 wt_parent,
                                                 peer_addr, peer_port,
                                                 email, more_info,
                                                 error);
}

int
syncwerk_cancel_clone_task (const char *repo_id, GError **error)
{
    return syncw_clone_manager_cancel_task (syncw->clone_mgr, repo_id);
}

int
syncwerk_remove_clone_task (const char *repo_id, GError **error)
{
    return syncw_clone_manager_remove_task (syncw->clone_mgr, repo_id);
}

GList *
syncwerk_get_clone_tasks (GError **error)
{
    GList *tasks, *ptr;
    GList *ret = NULL;
    CloneTask *task;
    SyncwerkCloneTask *t;

    tasks = syncw_clone_manager_get_tasks (syncw->clone_mgr);
    for (ptr = tasks; ptr != NULL; ptr = ptr->next) {
        task = ptr->data;
        t = g_object_new (SYNCWERK_TYPE_CLONE_TASK,
                          "state", clone_task_state_to_str(task->state),
                          "error_str", clone_task_error_to_str(task->error),
                          "repo_id", task->repo_id,
                          "repo_name", task->repo_name,
                          "worktree", task->worktree,
                          NULL);
        ret = g_list_prepend (ret, t);
    }

    g_list_free (tasks);
    return ret;
}

int
syncwerk_sync (const char *repo_id, const char *peer_id, GError **error)
{
    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Repo ID should not be null");
        return -1;
    }

    return syncw_sync_manager_add_sync_task (syncw->sync_mgr, repo_id, error);
}

static void get_task_size(TransferTask *task, gint64 *rsize, gint64 *dsize)
{
    if (task->runtime_state == TASK_RT_STATE_INIT
        || task->runtime_state == TASK_RT_STATE_COMMIT
        || task->runtime_state == TASK_RT_STATE_FS
        || task->runtime_state == TASK_RT_STATE_FINISHED) {
        *rsize = task->rsize;
        *dsize = task->dsize;
    }
    if (task->runtime_state == TASK_RT_STATE_DATA) {
        if (task->type == TASK_TYPE_DOWNLOAD) {
            *dsize = task->block_list->n_valid_blocks;
            *rsize = task->block_list->n_blocks - *dsize;
        } else {
            *dsize = task->n_uploaded;
            *rsize = task->block_list->n_blocks - *dsize;
        }
    }
}

static SyncwerkTask *
convert_task (TransferTask *task)
{
    gint64 rsize = 0, dsize = 0;
    SyncwerkTask *t = syncwerk_task_new();

    if (task->protocol_version < 7)
        get_task_size (task, &rsize, &dsize);

    g_object_set (t,
                  "repo_id", task->repo_id,
                  "state", task_state_to_str(task->state),
                  "rt_state", task_rt_state_to_str(task->runtime_state),
                  "error_str", task_error_str(task->error),
                  NULL);

    if (task->type == TASK_TYPE_DOWNLOAD) {
        g_object_set (t, "ttype", "download", NULL);
        if (task->runtime_state == TASK_RT_STATE_DATA) {
            if (task->protocol_version >= 7)
                g_object_set (t, "block_total", task->n_to_download,
                              "block_done", transfer_task_get_done_blocks (task),
                              NULL);
            else
                g_object_set (t, "block_total", task->block_list->n_blocks,
                              "block_done", transfer_task_get_done_blocks (task),
                              NULL);
            g_object_set (t, "rate", transfer_task_get_rate(task), NULL);
        }
    } else {
        g_object_set (t, "ttype", "upload", NULL);
        if (task->runtime_state == TASK_RT_STATE_DATA) {
            g_object_set (t, "block_total", task->block_list->n_blocks,
                          "block_done", transfer_task_get_done_blocks (task),
                          NULL);
            g_object_set (t, "rate", transfer_task_get_rate(task), NULL);
        }
    }

    return t;
}

static SyncwerkTask *
convert_http_task (HttpTxTask *task)
{
    SyncwerkTask *t = syncwerk_task_new();

    g_object_set (t,
                  "repo_id", task->repo_id,
                  "state", http_task_state_to_str(task->state),
                  "rt_state", http_task_rt_state_to_str(task->runtime_state),
                  "error_str", http_task_error_str(task->error),
                  NULL);

    if (task->type == HTTP_TASK_TYPE_DOWNLOAD) {
        g_object_set (t, "ttype", "download", NULL);
        if (task->runtime_state == HTTP_TASK_RT_STATE_BLOCK) {
            g_object_set (t, "block_total", task->n_files,
                          "block_done", task->done_files,
                          NULL);
            g_object_set (t, "rate", http_tx_task_get_rate(task), NULL);
        } else if (task->runtime_state == HTTP_TASK_RT_STATE_FS) {
            g_object_set (t, "fs_objects_total", task->n_fs_objs,
                          "fs_objects_done", task->done_fs_objs,
                          NULL);
        }
    } else {
        g_object_set (t, "ttype", "upload", NULL);
        if (task->runtime_state == HTTP_TASK_RT_STATE_BLOCK) {
            g_object_set (t, "block_total", task->n_blocks,
                          "block_done", task->done_blocks,
                          NULL);
            g_object_set (t, "rate", http_tx_task_get_rate(task), NULL);
        }
    }

    return t;
}

GObject *
syncwerk_find_transfer_task (const char *repo_id, GError *error)
{
    TransferTask *task;
    HttpTxTask *http_task;

    task = syncw_transfer_manager_find_transfer_by_repo (syncw->transfer_mgr, repo_id);
    if (task)
        return (GObject *)convert_task (task);

    http_task = http_tx_manager_find_task (syncw->http_tx_mgr, repo_id);
    if (http_task)
        return (GObject *)convert_http_task (http_task);

    return NULL;
}

int
syncwerk_get_upload_rate(GError **error)
{
    return syncw->sync_mgr->last_sent_bytes;
}

int
syncwerk_get_download_rate(GError **error)
{
    return syncw->sync_mgr->last_recv_bytes;
}


GObject *
syncwerk_get_repo_sync_info (const char *repo_id, GError **error)
{
    SyncInfo *info;

    info = syncw_sync_manager_get_sync_info (syncw->sync_mgr, repo_id);
    if (!info)
        return NULL;

    SyncwerkSyncInfo *sinfo;
    sinfo = g_object_new (SYNCWERK_TYPE_SYNC_INFO,
                          "repo_id", info->repo_id,
                          "head_commit", info->head_commit,
                          "deleted_on_relay", info->deleted_on_relay,
                          "need_fetch", info->need_fetch,
                          "need_upload", info->need_upload,
                          "need_merge", info->need_merge,
                          /* "last_sync_time", info->last_sync_time,  */
                          NULL);

    return (GObject *)sinfo;
}


GObject *
syncwerk_get_repo_sync_task (const char *repo_id, GError **error)
{
    SyncwRepo *repo;
    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);

    if (!repo) {
        return NULL;
    }

    SyncInfo *info = syncw_sync_manager_get_sync_info (syncw->sync_mgr, repo_id);
    if (!info || !info->current_task)
        return NULL;

    SyncTask *task = info->current_task;
    const char *sync_state;
    char allzeros[41] = {0};

    if (!info->in_sync && memcmp(allzeros, info->head_commit, 41) == 0) {
        sync_state = "waiting for sync";
    } else {
        sync_state = sync_state_to_str(task->state);
        if (strcmp(sync_state, "error") == 0 && !info->in_error)
            sync_state = "synchronized";
    }


    SyncwerkSyncTask *s_task;
    s_task = g_object_new (SYNCWERK_TYPE_SYNC_TASK,
                           "force_upload", task->is_manual_sync,
                           "state", sync_state,
                           "error", sync_error_to_str(task->error),
                           "repo_id", info->repo_id,
                           NULL);

    return (GObject *)s_task;
}

GList *
syncwerk_get_sync_task_list (GError **error)
{
    GHashTable *sync_info_tbl = syncw->sync_mgr->sync_infos;
    GHashTableIter iter;
    SyncwerkSyncTask *s_task;
    GList *task_list = NULL;
    gpointer key, value;

    g_hash_table_iter_init (&iter, sync_info_tbl);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        SyncInfo *info = value;
        if (!info->in_sync)
            continue;
        SyncTask *task = info->current_task;
        if (!task)
            continue;
        s_task = g_object_new (SYNCWERK_TYPE_SYNC_TASK,
                               "force_upload", task->is_manual_sync,
                               "state", sync_state_to_str(task->state),
                               "error", sync_error_to_str(task->error),
                               "repo_id", info->repo_id,
                               NULL);
        task_list = g_list_prepend (task_list, s_task);
    }

    return task_list;
}


int
syncwerk_set_repo_property (const char *repo_id,
                           const char *key,
                           const char *value,
                           GError **error)
{
    int ret;

    if (repo_id == NULL || key == NULL || value == NULL) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return -1;
    }

    SyncwRepo *repo;
    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_REPO, "Can't find Repo %s", repo_id);
        return -1;
    }

    ret = syncw_repo_manager_set_repo_property (syncw->repo_mgr,
                                               repo->id, key, value);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "Failed to set key for repo %s", repo_id);
        return -1;
    }

    return 0;
}

gchar *
syncwerk_get_repo_property (const char *repo_id,
                           const char *key,
                           GError **error)
{
    char *value = NULL;

    if (!repo_id || !key) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return NULL;
    }

    SyncwRepo *repo;
    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_REPO, "Can't find Repo %s", repo_id);
        return NULL;
    }

    value = syncw_repo_manager_get_repo_property (syncw->repo_mgr, repo->id, key);
    return value;
}

char *
syncwerk_get_repo_relay_address (const char *repo_id,
                                GError **error)
{
    char *relay_addr = NULL;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return NULL;
    }

    syncw_repo_manager_get_repo_relay_info (syncw->repo_mgr, repo_id,
                                           &relay_addr, NULL);

    return relay_addr;
}

char *
syncwerk_get_repo_relay_port (const char *repo_id,
                             GError **error)
{
    char *relay_port = NULL;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return NULL;
    }

    syncw_repo_manager_get_repo_relay_info (syncw->repo_mgr, repo_id,
                                           NULL, &relay_port);

    return relay_port;
}

int
syncwerk_update_repo_relay_info (const char *repo_id,
                                const char *new_addr,
                                const char *new_port,
                                GError **error)
{
    if (!repo_id || !new_addr || !new_port) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return -1;
    }

    int port = atoi(new_port);
    if (port <= 0 || port > 65535) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid port");
        return -1;
    }

    SyncwRepo *repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        return -1;
    }

    CcnetPeer *relay = ccnet_get_peer (syncw->ccnetrpc_client, repo->relay_id);
    if (!relay) {
        GString *buf = g_string_new(NULL);
        g_string_append_printf (buf, "add-relay --id %s --addr %s:%s",
                                repo->relay_id, new_addr, new_port);

        ccnet_send_command (syncw->session, buf->str, NULL, NULL);
        g_string_free (buf, TRUE);
    } else {
        if (g_strcmp0(relay->public_addr, new_addr) != 0 ||
            relay->public_port != (uint16_t)port) {
            ccnet_update_peer_address (syncw->ccnetrpc_client, repo->relay_id,
                                       new_addr, port);
        }

        g_object_unref (relay);
    }

    return syncw_repo_manager_update_repo_relay_info (syncw->repo_mgr, repo,
                                                     new_addr, new_port);
}

int
syncwerk_update_repos_server_host (const char *old_host,
                                  const char *new_host,
                                  const char *new_server_url,
                                  GError **error)
{
    if (!old_host || !new_host || !new_server_url) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    return syncw_repo_manager_update_repos_server_host(
        syncw->repo_mgr, old_host, new_host, new_server_url);
}

int
syncwerk_calc_dir_size (const char *path, GError **error)
{
    if (!path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    gint64 size_64 = ccnet_calc_directory_size(path, error);
    if (size_64 < 0) {
        syncw_warning ("failed to calculate dir size for %s\n", path);
        return -1;
    }

    /* get the size in MB */
    int size = (int) (size_64 >> 20);
    return size;
}

int
syncwerk_disable_auto_sync (GError **error)
{
    return syncw_sync_manager_disable_auto_sync (syncw->sync_mgr);
}

int
syncwerk_enable_auto_sync (GError **error)
{
    return syncw_sync_manager_enable_auto_sync (syncw->sync_mgr);
}

int syncwerk_is_auto_sync_enabled (GError **error)
{
    return syncw_sync_manager_is_auto_sync_enabled (syncw->sync_mgr);
}

char *
syncwerk_get_path_sync_status (const char *repo_id,
                              const char *path,
                              int is_dir,
                              GError **error)
{
    char *canon_path = NULL;
    int len;
    char *status;

    if (!repo_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    /* Empty path means to get status of the worktree folder. */
    if (strcmp (path, "") != 0) {
        if (*path == '/')
            ++path;
        canon_path = g_strdup(path);
        len = strlen(canon_path);
        if (canon_path[len-1] == '/')
            canon_path[len-1] = 0;
    } else {
        canon_path = g_strdup(path);
    }

    status = syncw_sync_manager_get_path_sync_status (syncw->sync_mgr,
                                                     repo_id,
                                                     canon_path,
                                                     is_dir);
    g_free (canon_path);
    return status;
}

int
syncwerk_mark_file_locked (const char *repo_id, const char *path, GError **error)
{
    char *canon_path = NULL;
    int len;
    int ret;

    if (!repo_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    if (*path == '/')
        ++path;

    if (path[0] == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid path");
        return -1;
    }

    canon_path = g_strdup(path);
    len = strlen(canon_path);
    if (canon_path[len-1] == '/')
        canon_path[len-1] = 0;

    ret = syncw_filelock_manager_mark_file_locked (syncw->filelock_mgr,
                                                  repo_id, path, FALSE);

    g_free (canon_path);
    return ret;
}

int
syncwerk_mark_file_unlocked (const char *repo_id, const char *path, GError **error)
{
    char *canon_path = NULL;
    int len;
    int ret;

    if (!repo_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    if (*path == '/')
        ++path;

    if (path[0] == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid path");
        return -1;
    }

    canon_path = g_strdup(path);
    len = strlen(canon_path);
    if (canon_path[len-1] == '/')
        canon_path[len-1] = 0;

    ret = syncw_filelock_manager_mark_file_unlocked (syncw->filelock_mgr,
                                                    repo_id, path);

    g_free (canon_path);
    return ret;
}

char *
syncwerk_get_server_property (const char *server_url, const char *key, GError **error)
{
    if (!server_url || !key) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Argument should not be null");
        return NULL;
    }

    return syncw_repo_manager_get_server_property (syncw->repo_mgr,
                                                  server_url,
                                                  key);
}

int
syncwerk_set_server_property (const char *server_url,
                             const char *key,
                             const char *value,
                             GError **error)
{
    if (!server_url || !key || !value) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Argument should not be null");
        return -1;
    }

    return syncw_repo_manager_set_server_property (syncw->repo_mgr,
                                                  server_url,
                                                  key, value);
}

#endif  /* not define SYNCWERK_SERVER */

/*
 * RPC functions available for both clients and server.
 */

GList *
syncwerk_branch_gets (const char *repo_id, GError **error)
{
    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    GList *blist = syncw_branch_manager_get_branch_list(syncw->branch_mgr,
                                                       repo_id);
    GList *ptr;
    GList *ret = NULL;

    for (ptr = blist; ptr; ptr=ptr->next) {
        SyncwBranch *b = ptr->data;
        SyncwerkBranch *branch = syncwerk_branch_new ();
        g_object_set (branch, "repo_id", b->repo_id, "name", b->name,
                      "commit_id", b->commit_id, NULL);
        ret = g_list_prepend (ret, branch);
        syncw_branch_unref (b);
    }
    ret = g_list_reverse (ret);
    g_list_free (blist);
    return ret;
}

#ifdef SYNCWERK_SERVER
GList*
syncwerk_get_trash_repo_list (int start, int limit, GError **error)
{
    return syncw_repo_manager_get_trash_repo_list (syncw->repo_mgr,
                                                  start, limit,
                                                  error);
}

GList *
syncwerk_get_trash_repos_by_owner (const char *owner, GError **error)
{
    if (!owner) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    return syncw_repo_manager_get_trash_repos_by_owner (syncw->repo_mgr,
                                                       owner,
                                                       error);
}

int
syncwerk_del_repo_from_trash (const char *repo_id, GError **error)
{
    int ret = 0;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }
    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    ret = syncw_repo_manager_del_repo_from_trash (syncw->repo_mgr, repo_id, error);

    return ret;
}

int
syncwerk_empty_repo_trash (GError **error)
{
    return syncw_repo_manager_empty_repo_trash (syncw->repo_mgr, error);
}

int
syncwerk_empty_repo_trash_by_owner (const char *owner, GError **error)
{
    if (!owner) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    return syncw_repo_manager_empty_repo_trash_by_owner (syncw->repo_mgr, owner, error);
}

int
syncwerk_restore_repo_from_trash (const char *repo_id, GError **error)
{
    int ret = 0;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }
    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    ret = syncw_repo_manager_restore_repo_from_trash (syncw->repo_mgr, repo_id, error);

    return ret;
}
#endif

GList*
syncwerk_get_repo_list (int start, int limit, GError **error)
{
    GList *repos = syncw_repo_manager_get_repo_list(syncw->repo_mgr, start, limit);
    GList *ret = NULL;

    ret = convert_repo_list (repos);

#ifdef SYNCWERK_SERVER
    GList *ptr;
    for (ptr = repos; ptr != NULL; ptr = ptr->next)
        syncw_repo_unref ((SyncwRepo *)ptr->data);
#endif
    g_list_free (repos);

    return ret;
}

#ifdef SYNCWERK_SERVER
gint64
syncwerk_count_repos (GError **error)
{
    return syncw_repo_manager_count_repos (syncw->repo_mgr, error);
}
#endif

GObject*
syncwerk_get_repo (const char *repo_id, GError **error)
{
    SyncwRepo *r;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }
    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    r = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    /* Don't return repo that's not checked out. */
    if (r == NULL)
        return NULL;

    GObject *repo = convert_repo (r);

#ifdef SYNCWERK_SERVER
    syncw_repo_unref (r);
#endif

    return repo;
}

SyncwerkCommit *
convert_to_syncwerk_commit (SyncwCommit *c)
{
    SyncwerkCommit *commit = syncwerk_commit_new ();
    g_object_set (commit,
                  "id", c->commit_id,
                  "creator_name", c->creator_name,
                  "creator", c->creator_id,
                  "desc", c->desc,
                  "ctime", c->ctime,
                  "repo_id", c->repo_id,
                  "root_id", c->root_id,
                  "parent_id", c->parent_id,
                  "second_parent_id", c->second_parent_id,
                  "version", c->version,
                  "new_merge", c->new_merge,
                  "conflict", c->conflict,
                  "device_name", c->device_name,
                  "client_version", c->client_version,
                  NULL);
    return commit;
}

GObject*
syncwerk_get_commit (const char *repo_id, int version,
                    const gchar *id, GError **error)
{
    SyncwerkCommit *commit;
    SyncwCommit *c;

    if (!repo_id || !is_uuid_valid(repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    if (!id || !is_object_id_valid(id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid commit id");
        return NULL;
    }

    c = syncw_commit_manager_get_commit (syncw->commit_mgr, repo_id, version, id);
    if (!c)
        return NULL;

    commit = convert_to_syncwerk_commit (c);
    syncw_commit_unref (c);
    return (GObject *)commit;
}

struct CollectParam {
    int offset;
    int limit;
    int count;
    GList *commits;
#ifdef SYNCWERK_SERVER
    gint64 truncate_time;
    gboolean traversed_head;
#endif
};

static gboolean
get_commit (SyncwCommit *c, void *data, gboolean *stop)
{
    struct CollectParam *cp = data;

#ifdef SYNCWERK_SERVER
    if (cp->truncate_time == 0)
    {
        *stop = TRUE;
        /* Stop after traversing the head commit. */
    }
    /* We use <= here. This is for handling clean trash and history.
     * If the user cleans all history, truncate time will be equal to
     * the commit's ctime. In such case, we don't actually want to display
     * this commit.
     */
    else if (cp->truncate_time > 0 &&
             (gint64)(c->ctime) <= cp->truncate_time &&
             cp->traversed_head)
    {
        *stop = TRUE;
        return TRUE;
    }

    /* Always traverse the head commit. */
    if (!cp->traversed_head)
        cp->traversed_head = TRUE;
#endif

    /* if offset = 1, limit = 1, we should stop when the count = 2 */
    if (cp->limit > 0 && cp->count >= cp->offset + cp->limit) {
        *stop = TRUE;
        return TRUE;  /* TRUE to indicate no error */
    }

    if (cp->count >= cp->offset) {
        SyncwerkCommit *commit = convert_to_syncwerk_commit (c);
        cp->commits = g_list_prepend (cp->commits, commit);
    }

    ++cp->count;
    return TRUE;                /* TRUE to indicate no error */
}


GList*
syncwerk_get_commit_list (const char *repo_id,
                         int offset,
                         int limit,
                         GError **error)
{
    SyncwRepo *repo;
    GList *commits = NULL;
    gboolean ret;
    struct CollectParam cp;
    char *commit_id;

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    /* correct parameter */
    if (offset < 0)
        offset = 0;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_REPO, "No such repository");
        return NULL;
    }

    if (!repo->head) {
        SyncwBranch *branch =
            syncw_branch_manager_get_branch (syncw->branch_mgr,
                                            repo->id, "master");
        if (branch != NULL) {
            commit_id = g_strdup (branch->commit_id);
            syncw_branch_unref (branch);
        } else {
            syncw_warning ("[repo-mgr] Failed to get repo %s branch master\n",
                       repo_id);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_REPO,
                         "No head and branch master");
#ifdef SYNCWERK_SERVER
            syncw_repo_unref (repo);
#endif
            return NULL;
        }
    } else {
        commit_id = g_strdup (repo->head->commit_id);
    }

    /* Init CollectParam */
    memset (&cp, 0, sizeof(cp));
    cp.offset = offset;
    cp.limit = limit;

#ifdef SYNCWERK_SERVER
    cp.truncate_time = syncw_repo_manager_get_repo_truncate_time (syncw->repo_mgr,
                                                                 repo_id);
#endif

    ret =
        syncw_commit_manager_traverse_commit_tree (syncw->commit_mgr,
                                                  repo->id, repo->version,
                                                  commit_id, get_commit, &cp, TRUE);
    g_free (commit_id);
#ifdef SYNCWERK_SERVER
    syncw_repo_unref (repo);
#endif

    if (!ret) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_LIST_COMMITS, "Failed to list commits");
        return NULL;
    }

    commits = g_list_reverse (cp.commits);
    return commits;
}

#ifndef SYNCWERK_SERVER
static
int do_unsync_repo(SyncwRepo *repo)
{
    if (!syncw->started) {
        syncw_message ("System not started, skip removing repo.\n");
        return -1;
    }

    if (repo->auto_sync && (repo->sync_interval == 0))
        syncw_wt_monitor_unwatch_repo (syncw->wt_monitor, repo->id);

    syncw_sync_manager_cancel_sync_task (syncw->sync_mgr, repo->id);

    SyncInfo *info = syncw_sync_manager_get_sync_info (syncw->sync_mgr, repo->id);

    /* If we are syncing the repo,
     * we just mark the repo as deleted and let sync-mgr actually delete it.
     * Otherwise we are safe to delete the repo.
     */
    char *worktree = g_strdup (repo->worktree);
    if (info != NULL && info->in_sync) {
        syncw_repo_manager_mark_repo_deleted (syncw->repo_mgr, repo);
    } else {
        syncw_repo_manager_del_repo (syncw->repo_mgr, repo);
    }

    g_free (worktree);

    return 0;
}

static void
cancel_clone_tasks_by_account (const char *account_server, const char *account_email)
{
    GList *ptr, *tasks;
    CloneTask *task;

    tasks = syncw_clone_manager_get_tasks (syncw->clone_mgr);
    for (ptr = tasks; ptr != NULL; ptr = ptr->next) {
        task = ptr->data;

        if (g_strcmp0(account_server, task->peer_addr) == 0
            && g_strcmp0(account_email, task->email) == 0) {
            syncw_clone_manager_cancel_task (syncw->clone_mgr, task->repo_id);
        }
    }

    g_list_free (tasks);
}

int
syncwerk_unsync_repos_by_account (const char *server_addr, const char *email, GError **error)
{
    if (!server_addr || !email) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    GList *ptr, *repos = syncw_repo_manager_get_repo_list(syncw->repo_mgr, -1, -1);
    if (!repos) {
        return 0;
    }

    for (ptr = repos; ptr; ptr = ptr->next) {
        SyncwRepo *repo = (SyncwRepo*)ptr->data;
        char *addr = NULL;
        syncw_repo_manager_get_repo_relay_info(syncw->repo_mgr,
                                              repo->id,
                                              &addr, /* addr */
                                              NULL); /* port */

        if (g_strcmp0(addr, server_addr) == 0 && g_strcmp0(repo->email, email) == 0) {
            if (do_unsync_repo(repo) < 0) {
                return -1;
            }
        }

        g_free (addr);
    }

    g_list_free (repos);

    cancel_clone_tasks_by_account (server_addr, email);

    return 0;
}

int
syncwerk_remove_repo_tokens_by_account (const char *server_addr, const char *email, GError **error)
{
    if (!server_addr || !email) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    GList *ptr, *repos = syncw_repo_manager_get_repo_list(syncw->repo_mgr, -1, -1);
    if (!repos) {
        return 0;
    }

    for (ptr = repos; ptr; ptr = ptr->next) {
        SyncwRepo *repo = (SyncwRepo*)ptr->data;
        char *addr = NULL;
        syncw_repo_manager_get_repo_relay_info(syncw->repo_mgr,
                                              repo->id,
                                              &addr, /* addr */
                                              NULL); /* port */

        if (g_strcmp0(addr, server_addr) == 0 && g_strcmp0(repo->email, email) == 0) {
            if (syncw_repo_manager_remove_repo_token(syncw->repo_mgr, repo) < 0) {
                return -1;
            }
        }

        g_free (addr);
    }

    g_list_free (repos);

    cancel_clone_tasks_by_account (server_addr, email);

    return 0;
}

int
syncwerk_set_repo_token (const char *repo_id,
                        const char *token,
                        GError **error)
{
    int ret;

    if (repo_id == NULL || token == NULL) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return -1;
    }

    SyncwRepo *repo;
    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_REPO, "Can't find Repo %s", repo_id);
        return -1;
    }

    ret = syncw_repo_manager_set_repo_token (syncw->repo_mgr,
                                            repo, token);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "Failed to set token for repo %s", repo_id);
        return -1;
    }

    return 0;
}

#endif

int
syncwerk_destroy_repo (const char *repo_id, GError **error)
{
    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }
    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

#ifndef SYNCWERK_SERVER
    SyncwRepo *repo;

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "No such repository");
        return -1;
    }

    return do_unsync_repo(repo);
#else

    return syncw_repo_manager_del_repo (syncw->repo_mgr, repo_id, error);
#endif
}


GObject *
syncwerk_generate_magic_and_random_key(int enc_version,
                                      const char* repo_id,
                                      const char *passwd,
                                      GError **error)
{
    if (!repo_id || !passwd) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    gchar magic[65] = {0};
    gchar random_key[97] = {0};

    syncwerk_generate_magic (CURRENT_ENC_VERSION, repo_id, passwd, magic);
    syncwerk_generate_random_key (passwd, random_key);

    SyncwerkEncryptionInfo *sinfo;
    sinfo = g_object_new (SYNCWERK_TYPE_ENCRYPTION_INFO,
                          "repo_id", repo_id,
                          "passwd", passwd,
                          "enc_version", CURRENT_ENC_VERSION,
                          "magic", magic,
                          "random_key", random_key,
                          NULL);

    return (GObject *)sinfo;

}

#include "diff-simple.h"

inline static const char*
get_diff_status_str(char status)
{
    if (status == DIFF_STATUS_ADDED)
        return "add";
    if (status == DIFF_STATUS_DELETED)
        return "del";
    if (status == DIFF_STATUS_MODIFIED)
        return "mod";
    if (status == DIFF_STATUS_RENAMED)
        return "mov";
    if (status == DIFF_STATUS_DIR_ADDED)
        return "newdir";
    if (status == DIFF_STATUS_DIR_DELETED)
        return "deldir";
    return NULL;
}

GList *
syncwerk_diff (const char *repo_id, const char *arg1, const char *arg2, int fold_dir_results, GError **error)
{
    SyncwRepo *repo;
    char *err_msgs = NULL;
    GList *diff_entries, *p;
    GList *ret = NULL;

    if (!repo_id || !arg1 || !arg2) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    if ((arg1[0] != 0 && !is_object_id_valid (arg1)) || !is_object_id_valid(arg2)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid commit id");
        return NULL;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "No such repository");
        return NULL;
    }

    diff_entries = syncw_repo_diff (repo, arg1, arg2, fold_dir_results, &err_msgs);
    if (err_msgs) {
        g_set_error (error, SYNCWERK_DOMAIN, -1, "%s", err_msgs);
        g_free (err_msgs);
#ifdef SYNCWERK_SERVER
        syncw_repo_unref (repo);
#endif
        return NULL;
    }

#ifdef SYNCWERK_SERVER
    syncw_repo_unref (repo);
#endif

    for (p = diff_entries; p != NULL; p = p->next) {
        DiffEntry *de = p->data;
        SyncwerkDiffEntry *entry = g_object_new (
            SYNCWERK_TYPE_DIFF_ENTRY,
            "status", get_diff_status_str(de->status),
            "name", de->name,
            "new_name", de->new_name,
            NULL);
        ret = g_list_prepend (ret, entry);
    }

    for (p = diff_entries; p != NULL; p = p->next) {
        DiffEntry *de = p->data;
        diff_entry_free (de);
    }
    g_list_free (diff_entries);

    return g_list_reverse (ret);
}

/*
 * RPC functions only available for server.
 */

#ifdef SYNCWERK_SERVER

GList *
syncwerk_list_dir_by_path(const char *repo_id,
                         const char *commit_id,
                         const char *path, GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *commit = NULL;
    SyncwDir *dir;
    SyncwDirent *dent;
    SyncwerkDirent *d;

    GList *ptr;
    GList *res = NULL;

    if (!repo_id || !commit_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Args can't be NULL");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo id");
        return NULL;
    }

    if (!is_object_id_valid (commit_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid commit id");
        return NULL;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad repo id");
        return NULL;
    }

    commit = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                             repo_id, repo->version,
                                             commit_id);

    if (!commit) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_COMMIT, "No such commit");
        goto out;
    }

    char *rpath = format_dir_path (path);
    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               repo->store_id,
                                               repo->version,
                                               commit->root_id,
                                               rpath, error);
    g_free (rpath);

    if (!dir) {
        syncw_warning ("Can't find syncw dir for %s in repo %s\n", path, repo->store_id);
        goto out;
    }

    for (ptr = dir->entries; ptr != NULL; ptr = ptr->next) {
        dent = ptr->data;

        if (!is_object_id_valid (dent->id))
            continue;

        d = g_object_new (SYNCWERK_TYPE_DIRENT,
                          "obj_id", dent->id,
                          "obj_name", dent->name,
                          "mode", dent->mode,
                          "version", dent->version,
                          "mtime", dent->mtime,
                          "size", dent->size,
                          NULL);
        res = g_list_prepend (res, d);
    }

    syncw_dir_free (dir);
    res = g_list_reverse (res);

out:
    syncw_repo_unref (repo);
    syncw_commit_unref (commit);
    return res;
}

static void
filter_error (GError **error)
{
    if (*error && g_error_matches(*error,
                                  SYNCWERK_DOMAIN,
                                  SYNCW_ERR_PATH_NO_EXIST)) {
        g_clear_error (error);
    }
}

char *
syncwerk_get_dir_id_by_commit_and_path(const char *repo_id,
                                      const char *commit_id,
                                      const char *path,
                                      GError **error)
{
    SyncwRepo *repo = NULL;
    char *res = NULL;
    SyncwCommit *commit = NULL;
    SyncwDir *dir;

    if (!repo_id || !commit_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Args can't be NULL");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo id");
        return NULL;
    }

    if (!is_object_id_valid (commit_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid commit id");
        return NULL;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad repo id");
        return NULL;
    }

    commit = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                             repo_id, repo->version,
                                             commit_id);

    if (!commit) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_COMMIT, "No such commit");
        goto out;
    }

    char *rpath = format_dir_path (path);

    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               repo->store_id,
                                               repo->version,
                                               commit->root_id,
                                               rpath, error);
    g_free (rpath);

    if (!dir) {
        syncw_warning ("Can't find syncw dir for %s in repo %s\n", path, repo->store_id);
        filter_error (error);
        goto out;
    }

    res = g_strdup (dir->dir_id);
    syncw_dir_free (dir);

 out:
    syncw_repo_unref (repo);
    syncw_commit_unref (commit);
    return res;
}

int
syncwerk_edit_repo (const char *repo_id,
                   const char *name,
                   const char *description,
                   const char *user,
                   GError **error)
{
    return syncw_repo_manager_edit_repo (repo_id, name, description, user, error);
}

int
syncwerk_change_repo_passwd (const char *repo_id,
                            const char *old_passwd,
                            const char *new_passwd,
                            const char *user,
                            GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *commit = NULL, *parent = NULL;
    int ret = 0;

    if (!user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "No user given");
        return -1;
    }

    if (!old_passwd || old_passwd[0] == 0 || !new_passwd || new_passwd[0] == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Empty passwd");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

retry:
    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "No such library");
        return -1;
    }

    if (!repo->encrypted) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Repo not encrypted");
        return -1;
    }

    if (repo->enc_version < 2) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Unsupported enc version");
        return -1;
    }

    if (syncwerk_verify_repo_passwd (repo_id, old_passwd, repo->magic, 2) < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Incorrect password");
        return -1;
    }

    parent = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                             repo->id, repo->version,
                                             repo->head->commit_id);
    if (!parent) {
        syncw_warning ("Failed to get commit %s:%s.\n",
                      repo->id, repo->head->commit_id);
        ret = -1;
        goto out;
    }

    char new_magic[65], new_random_key[97];

    syncwerk_generate_magic (2, repo_id, new_passwd, new_magic);
    if (syncwerk_update_random_key (old_passwd, repo->random_key,
                                   new_passwd, new_random_key) < 0) {
        ret = -1;
        goto out;
    }

    memcpy (repo->magic, new_magic, 64);
    memcpy (repo->random_key, new_random_key, 96);

    commit = syncw_commit_new (NULL,
                              repo->id,
                              parent->root_id,
                              user,
                              EMPTY_SHA1,
                              "Changed library password",
                              0);
    commit->parent_id = g_strdup(parent->commit_id);
    syncw_repo_to_commit (repo, commit);

    if (syncw_commit_manager_add_commit (syncw->commit_mgr, commit) < 0) {
        ret = -1;
        goto out;
    }

    syncw_branch_set_commit (repo->head, commit->commit_id);
    if (syncw_branch_manager_test_and_update_branch (syncw->branch_mgr,
                                                    repo->head,
                                                    parent->commit_id) < 0) {
        syncw_repo_unref (repo);
        syncw_commit_unref (commit);
        syncw_commit_unref (parent);
        repo = NULL;
        commit = NULL;
        parent = NULL;
        goto retry;
    }

    if (syncw_passwd_manager_is_passwd_set (syncw->passwd_mgr, repo_id, user))
        syncw_passwd_manager_set_passwd (syncw->passwd_mgr, repo_id,
                                        user, new_passwd, error);

out:
    syncw_commit_unref (commit);
    syncw_commit_unref (parent);
    syncw_repo_unref (repo);

    return ret;
}

int
syncwerk_is_repo_owner (const char *email,
                       const char *repo_id,
                       GError **error)
{
    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return 0;
    }

    char *owner = syncw_repo_manager_get_repo_owner (syncw->repo_mgr, repo_id);
    if (!owner) {
        /* syncw_warning ("Failed to get owner info for repo %s.\n", repo_id); */
        return 0;
    }

    if (strcmp(owner, email) != 0) {
        g_free (owner);
        return 0;
    }

    g_free (owner);
    return 1;
}

int
syncwerk_set_repo_owner(const char *repo_id, const char *email,
                       GError **error)
{
    if (!repo_id || !email) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }
    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    return syncw_repo_manager_set_repo_owner(syncw->repo_mgr, repo_id, email);
}

char *
syncwerk_get_repo_owner (const char *repo_id, GError **error)
{
    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }
    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    char *owner = syncw_repo_manager_get_repo_owner (syncw->repo_mgr, repo_id);
    /* if (!owner){ */
    /*     syncw_warning ("Failed to get repo owner for repo %s.\n", repo_id); */
    /* } */

    return owner;
}

GList *
syncwerk_get_orphan_repo_list(GError **error)
{
    GList *ret = NULL;
    GList *repos, *ptr;

    repos = syncw_repo_manager_get_orphan_repo_list(syncw->repo_mgr);
    ret = convert_repo_list (repos);

    for (ptr = repos; ptr; ptr = ptr->next) {
        syncw_repo_unref ((SyncwRepo *)ptr->data);
    }
    g_list_free (repos);

    return ret;
}

GList *
syncwerk_list_owned_repos (const char *email, int ret_corrupted,
                          int start, int limit, GError **error)
{
    GList *ret = NULL;
    GList *repos, *ptr;

    repos = syncw_repo_manager_get_repos_by_owner (syncw->repo_mgr, email, ret_corrupted,
                                                  start, limit);
    ret = convert_repo_list (repos);

    /* for (ptr = ret; ptr; ptr = ptr->next) { */
    /*     g_object_get (ptr->data, "repo_id", &repo_id, NULL); */
    /*     is_shared = syncw_share_manager_is_repo_shared (syncw->share_mgr, repo_id); */
    /*     if (is_shared < 0) { */
    /*         g_free (repo_id); */
    /*         break; */
    /*     } else { */
    /*         g_object_set (ptr->data, "is_shared", is_shared, NULL); */
    /*         g_free (repo_id); */
    /*     } */
    /* } */

    /* while (ptr) { */
    /*     g_object_set (ptr->data, "is_shared", FALSE, NULL); */
    /*     ptr = ptr->prev; */
    /* } */

    for(ptr = repos; ptr; ptr = ptr->next) {
        syncw_repo_unref ((SyncwRepo *)ptr->data);
    }
    g_list_free (repos);

    return ret;
}

int
syncwerk_add_chunk_server (const char *server, GError **error)
{
    SyncwCSManager *cs_mgr = syncw->cs_mgr;
    CcnetPeer *peer;

    peer = ccnet_get_peer_by_idname (syncw->ccnetrpc_client, server);
    if (!peer) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid peer id or name %s", server);
        return -1;
    }

    if (syncw_cs_manager_add_chunk_server (cs_mgr, peer->id) < 0) {
        g_object_unref (peer);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "Failed to add chunk server %s", server);
        return -1;
    }

    g_object_unref (peer);
    return 0;
}

int
syncwerk_del_chunk_server (const char *server, GError **error)
{
    SyncwCSManager *cs_mgr = syncw->cs_mgr;
    CcnetPeer *peer;

    peer = ccnet_get_peer_by_idname (syncw->ccnetrpc_client, server);
    if (!peer) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid peer id or name %s", server);
        return -1;
    }

    if (syncw_cs_manager_del_chunk_server (cs_mgr, peer->id) < 0) {
        g_object_unref (peer);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "Failed to delete chunk server %s", server);
        return -1;
    }

    g_object_unref (peer);
    return 0;
}

char *
syncwerk_list_chunk_servers (GError **error)
{
    SyncwCSManager *cs_mgr = syncw->cs_mgr;
    GList *servers, *ptr;
    char *cs_id;
    CcnetPeer *peer;
    GString *buf = g_string_new ("");

    servers = syncw_cs_manager_get_chunk_servers (cs_mgr);
    ptr = servers;
    while (ptr) {
        cs_id = ptr->data;
        peer = ccnet_get_peer (syncw->ccnetrpc_client, cs_id);
        if (!peer) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "Internal error");
            g_string_free (buf, TRUE);
            return NULL;
        }
        g_object_unref (peer);

        g_string_append_printf (buf, "%s\n", cs_id);
        ptr = ptr->next;
    }
    g_list_free (servers);

    return (g_string_free (buf, FALSE));
}

gint64
syncwerk_get_user_quota_usage (const char *email, GError **error)
{
    gint64 ret;

    if (!email) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad user id");
        return -1;
    }

    ret = syncw_quota_manager_get_user_usage (syncw->quota_mgr, email);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Internal server error");
        return -1;
    }

    return ret;
}

gint64
syncwerk_get_user_share_usage (const char *email, GError **error)
{
    gint64 ret;

    if (!email) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad user id");
        return -1;
    }

    ret = syncw_quota_manager_get_user_share_usage (syncw->quota_mgr, email);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Internal server error");
        return -1;
    }

    return ret;
}

gint64
syncwerk_server_repo_size(const char *repo_id, GError **error)
{
    gint64 ret;

    if (!repo_id || strlen(repo_id) != 36) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad repo id");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    ret = syncw_repo_manager_get_repo_size (syncw->repo_mgr, repo_id);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Internal server error");
        return -1;
    }

    return ret;
}

int
syncwerk_set_repo_history_limit (const char *repo_id,
                                int days,
                                GError **error)
{
    if (!repo_id || !is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (syncw_repo_manager_set_repo_history_limit (syncw->repo_mgr,
                                                  repo_id,
                                                  days) < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL, "DB Error");
        return -1;
    }

    return 0;
}

int
syncwerk_get_repo_history_limit (const char *repo_id,
                                GError **error)
{
    if (!repo_id || !is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    return  syncw_repo_manager_get_repo_history_limit (syncw->repo_mgr, repo_id);
}

int
syncwerk_repo_set_access_property (const char *repo_id, const char *ap, GError **error)
{
    int ret;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    if (strlen(repo_id) != 36) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Wrong repo id");
        return -1;
    }

    if (g_strcmp0(ap, "public") != 0 && g_strcmp0(ap, "own") != 0 && g_strcmp0(ap, "private") != 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Wrong access property");
        return -1;
    }

    ret = syncw_repo_manager_set_access_property (syncw->repo_mgr, repo_id, ap);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Internal server error");
        return -1;
    }

    return ret;
}

char *
syncwerk_repo_query_access_property (const char *repo_id, GError **error)
{
    char *ret;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    if (strlen(repo_id) != 36) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Wrong repo id");
        return NULL;
    }

    ret = syncw_repo_manager_query_access_property (syncw->repo_mgr, repo_id);

    return ret;
}

char *
syncwerk_web_get_access_token (const char *repo_id,
                              const char *obj_id,
                              const char *op,
                              const char *username,
                              int use_onetime,
                              GError **error)
{
    char *token;

    if (!repo_id || !obj_id || !op || !username) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Missing args");
        return NULL;
    }

    token = syncw_web_at_manager_get_access_token (syncw->web_at_mgr,
                                                  repo_id, obj_id, op,
                                                  username, use_onetime, error);
    return token;
}

GObject *
syncwerk_web_query_access_token (const char *token, GError **error)
{
    SyncwerkWebAccess *webaccess = NULL;

    if (!token) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Token should not be null");
        return NULL;
    }

    webaccess = syncw_web_at_manager_query_access_token (syncw->web_at_mgr,
                                                        token);
    if (webaccess)
        return (GObject *)webaccess;

    return NULL;
}

char *
syncwerk_query_zip_progress (const char *token, GError **error)
{
    return zip_download_mgr_query_zip_progress (syncw->zip_download_mgr,
                                                token, error);
}

int
syncwerk_cancel_zip_task (const char *token, GError **error)
{
    return zip_download_mgr_cancel_zip_task (syncw->zip_download_mgr,
                                             token);
}

int
syncwerk_add_share (const char *repo_id, const char *from_email,
                   const char *to_email, const char *permission, GError **error)
{
    int ret;

    if (!repo_id || !from_email || !to_email || !permission) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Missing args");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo_id parameter");
        return -1;
    }

    if (g_strcmp0 (from_email, to_email) == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Can not share repo to myself");
        return -1;
    }

    if (!is_permission_valid (permission)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid permission parameter");
        return -1;
    }

    ret = syncw_share_manager_add_share (syncw->share_mgr, repo_id, from_email,
                                        to_email, permission);

    return ret;
}

GList *
syncwerk_list_share_repos (const char *email, const char *type,
                          int start, int limit, GError **error)
{
    if (g_strcmp0 (type, "from_email") != 0 &&
        g_strcmp0 (type, "to_email") != 0 ) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Wrong type argument");
        return NULL;
    }

    return syncw_share_manager_list_share_repos (syncw->share_mgr,
                                                email, type,
                                                start, limit);
}

GList *
syncwerk_list_repo_shared_to (const char *from_user, const char *repo_id,
                             GError **error)
{

    if (!from_user || !repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Missing args");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    return syncw_share_manager_list_repo_shared_to (syncw->share_mgr,
                                                   from_user, repo_id,
                                                   error);
}

char *
syncwerk_share_subdir_to_user (const char *repo_id,
                              const char *path,
                              const char *owner,
                              const char *share_user,
                              const char *permission,
                              const char *passwd,
                              GError **error)
{
    if (is_empty_string (repo_id) || !is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo_id parameter");
        return NULL;
    }

    if (is_empty_string (path) || strcmp (path, "/") == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid path parameter");
        return NULL;
    }

    if (is_empty_string (owner)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid owner parameter");
        return NULL;
    }

    if (is_empty_string (share_user)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid share_user parameter");
        return NULL;
    }

    if (strcmp (owner, share_user) == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Can't share subdir to myself");
        return NULL;
    }

    if (!is_permission_valid (permission)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid permission parameter");
        return NULL;
    }

    char *real_path;
    char *vrepo_name;
    char *vrepo_id;
    char *ret = NULL;

    real_path = format_dir_path (path);
    // Use subdir name as virtual repo name and description
    vrepo_name = g_path_get_basename (real_path);
    vrepo_id = syncw_repo_manager_create_virtual_repo (syncw->repo_mgr,
                                                      repo_id, real_path,
                                                      vrepo_name, vrepo_name,
                                                      owner, passwd, error);
    if (!vrepo_id)
        goto out;

    int result = syncw_share_manager_add_share (syncw->share_mgr, vrepo_id, owner,
                                        share_user, permission);
    if (result < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to share subdir to user");
        g_free (vrepo_id);
    }
    else 
        ret = vrepo_id;

out:
    g_free (vrepo_name);
    g_free (real_path);
    return ret;
}

int
syncwerk_unshare_subdir_for_user (const char *repo_id,
                                 const char *path,
                                 const char *owner,
                                 const char *share_user,
                                 GError **error)
{
    if (is_empty_string (repo_id) || !is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo_id parameter");
        return -1;
    }

    if (is_empty_string (path) || strcmp (path, "/") == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid path parameter");
        return -1;
    }

    if (is_empty_string (owner)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid owner parameter");
        return -1;
    }

    if (is_empty_string (share_user) ||
        strcmp (owner, share_user) == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid share_user parameter");
        return -1;
    }

    char *real_path;
    int ret = 0;

    real_path = format_dir_path (path);

    ret = syncw_share_manager_unshare_subdir (syncw->share_mgr,
                                             repo_id, real_path, owner, share_user);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to unshare subdir for user");
    }

    g_free (real_path);
    return ret;
}

int
syncwerk_update_share_subdir_perm_for_user (const char *repo_id,
                                           const char *path,
                                           const char *owner,
                                           const char *share_user,
                                           const char *permission,
                                           GError **error)
{
    if (is_empty_string (repo_id) || !is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo_id parameter");
        return -1;
    }

    if (is_empty_string (path) || strcmp (path, "/") == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid path parameter");
        return -1;
    }

    if (is_empty_string (owner)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid owner parameter");
        return -1;
    }

    if (is_empty_string (share_user) ||
        strcmp (owner, share_user) == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid share_user parameter");
        return -1;
    }

    if (!is_permission_valid (permission)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid permission parameter");
        return -1;
    }

    char *real_path;
    int ret = 0;

    real_path = format_dir_path (path);

    ret = syncw_share_manager_set_subdir_perm_by_path (syncw->share_mgr,
                                                      repo_id, owner, share_user,
                                                      permission, real_path);

    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to update share subdir permission for user");
    }

    g_free (real_path);
    return ret;
}

GList *
syncwerk_list_repo_shared_group (const char *from_user, const char *repo_id,
                                GError **error)
{

    if (!from_user || !repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Missing args");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    return syncw_share_manager_list_repo_shared_group (syncw->share_mgr,
                                                      from_user, repo_id,
                                                      error);
}

int
syncwerk_remove_share (const char *repo_id, const char *from_email,
                      const char *to_email, GError **error)
{
    int ret;

    if (!repo_id || !from_email ||!to_email) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Missing args");
        return -1;
    }

    ret = syncw_share_manager_remove_share (syncw->share_mgr, repo_id, from_email,
                                           to_email);

    return ret;
}

/* Group repo RPC. */

int
syncwerk_group_share_repo (const char *repo_id, int group_id,
                          const char *user_name, const char *permission,
                          GError **error)
{
    SyncwRepoManager *mgr = syncw->repo_mgr;
    int ret;

    if (group_id <= 0 || !user_name || !repo_id || !permission) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad input argument");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (!is_permission_valid (permission)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid permission parameter");
        return -1;
    }

    ret = syncw_repo_manager_add_group_repo (mgr, repo_id, group_id, user_name,
                                            permission, error);

    return ret;
}

int
syncwerk_group_unshare_repo (const char *repo_id, int group_id,
                            const char *user_name, GError **error)
{
    SyncwRepoManager *mgr = syncw->repo_mgr;
    int ret;

    if (!user_name || !repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "User name and repo id can not be NULL");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    ret = syncw_repo_manager_del_group_repo (mgr, repo_id, group_id, error);

    return ret;

}

char *
syncwerk_share_subdir_to_group (const char *repo_id,
                               const char *path,
                               const char *owner,
                               int share_group,
                               const char *permission,
                               const char *passwd,
                               GError **error)
{
    if (is_empty_string (repo_id) || !is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo_id parameter");
        return NULL;
    }

    if (is_empty_string (path) || strcmp (path, "/") == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid path parameter");
        return NULL;
    }

    if (is_empty_string (owner)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid owner parameter");
        return NULL;
    }

    if (share_group < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid share_group parameter");
        return NULL;
    }

    if (!is_permission_valid (permission)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid permission parameter");
        return NULL;
    }

    char *real_path;
    char *vrepo_name;
    char *vrepo_id;
    char* ret = NULL;

    real_path = format_dir_path (path);
    // Use subdir name as virtual repo name and description
    vrepo_name = g_path_get_basename (real_path);
    vrepo_id = syncw_repo_manager_create_virtual_repo (syncw->repo_mgr,
                                                      repo_id, real_path,
                                                      vrepo_name, vrepo_name,
                                                      owner, passwd, error);
    if (!vrepo_id)
        goto out;

    int result = syncw_repo_manager_add_group_repo (syncw->repo_mgr, vrepo_id, share_group,
                                            owner, permission, error);
    if (result < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to share subdir to group");
        g_free (vrepo_id);
    }
    else
        ret = vrepo_id;

out:
    g_free (vrepo_name);
    g_free (real_path);
    return ret;
}

int
syncwerk_unshare_subdir_for_group (const char *repo_id,
                                  const char *path,
                                  const char *owner,
                                  int share_group,
                                  GError **error)
{
    if (is_empty_string (repo_id) || !is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo_id parameter");
        return -1;
    }

    if (is_empty_string (path) || strcmp (path, "/") == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid path parameter");
        return -1;
    }

    if (is_empty_string (owner)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid owner parameter");
        return -1;
    }

    if (share_group < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid share_group parameter");
        return -1;
    }

    char *real_path;
    int ret = 0;

    real_path = format_dir_path (path);

    ret = syncw_share_manager_unshare_group_subdir (syncw->share_mgr, repo_id,
                                                   real_path, owner, share_group);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to unshare subdir for group");
    }

    g_free (real_path);
    return ret;
}

int
syncwerk_update_share_subdir_perm_for_group (const char *repo_id,
                                            const char *path,
                                            const char *owner,
                                            int share_group,
                                            const char *permission,
                                            GError **error)
{
    if (is_empty_string (repo_id) || !is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo_id parameter");
        return -1;
    }

    if (is_empty_string (path) || strcmp (path, "/") == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid path parameter");
        return -1;
    }

    if (is_empty_string (owner)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid owner parameter");
        return -1;
    }

    if (share_group < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid share_group parameter");
        return -1;
    }

    if (!is_permission_valid (permission)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid permission parameter");
        return -1;
    }

    char *real_path;
    int ret = 0;

    real_path = format_dir_path (path);
    ret = syncw_repo_manager_set_subdir_group_perm_by_path (syncw->repo_mgr,
                                                           repo_id, owner, share_group,
                                                           permission, real_path);
    if (ret < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to update share subdir permission for group");
    }

    g_free (real_path);
    return ret;
}

char *
syncwerk_get_shared_groups_by_repo(const char *repo_id, GError **error)
{
    SyncwRepoManager *mgr = syncw->repo_mgr;
    GList *group_ids = NULL, *ptr;
    GString *result;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    group_ids = syncw_repo_manager_get_groups_by_repo (mgr, repo_id, error);
    if (!group_ids) {
        return NULL;
    }

    result = g_string_new("");
    ptr = group_ids;
    while (ptr) {
        g_string_append_printf (result, "%d\n", (int)(long)ptr->data);
        ptr = ptr->next;
    }
    g_list_free (group_ids);

    return g_string_free (result, FALSE);
}

char *
syncwerk_get_group_repoids (int group_id, GError **error)
{
    SyncwRepoManager *mgr = syncw->repo_mgr;
    GList *repo_ids = NULL, *ptr;
    GString *result;

    repo_ids = syncw_repo_manager_get_group_repoids (mgr, group_id, error);
    if (!repo_ids) {
        return NULL;
    }

    result = g_string_new("");
    ptr = repo_ids;
    while (ptr) {
        g_string_append_printf (result, "%s\n", (char *)ptr->data);
        g_free (ptr->data);
        ptr = ptr->next;
    }
    g_list_free (repo_ids);

    return g_string_free (result, FALSE);
}

GList *
syncwerk_get_repos_by_group (int group_id, GError **error)
{
    SyncwRepoManager *mgr = syncw->repo_mgr;
    GList *ret = NULL;

    if (group_id < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid group id.");
        return NULL;
    }

    ret = syncw_repo_manager_get_repos_by_group (mgr, group_id, error);

    return ret;
}

GList *
syncwerk_get_group_repos_by_owner (char *user, GError **error)
{
    SyncwRepoManager *mgr = syncw->repo_mgr;
    GList *ret = NULL;

    if (!user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "user name can not be NULL");
        return NULL;
    }

    ret = syncw_repo_manager_get_group_repos_by_owner (mgr, user, error);
    if (!ret) {
        return NULL;
    }

    return g_list_reverse (ret);
}

char *
syncwerk_get_group_repo_owner (const char *repo_id, GError **error)
{
    SyncwRepoManager *mgr = syncw->repo_mgr;
    GString *result = g_string_new ("");

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    char *share_from = syncw_repo_manager_get_group_repo_owner (mgr, repo_id,
                                                               error);
    if (share_from) {
        g_string_append_printf (result, "%s", share_from);
        g_free (share_from);
    }

    return g_string_free (result, FALSE);
}

int
syncwerk_remove_repo_group(int group_id, const char *username, GError **error)
{
    if (group_id <= 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Wrong group id argument");
        return -1;
    }

    return syncw_repo_manager_remove_group_repos (syncw->repo_mgr,
                                                 group_id, username,
                                                 error);
}

/* Inner public repo RPC */

int
syncwerk_set_inner_pub_repo (const char *repo_id,
                            const char *permission,
                            GError **error)
{
    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad args");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (syncw_repo_manager_set_inner_pub_repo (syncw->repo_mgr,
                                              repo_id, permission) < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Internal error");
        return -1;
    }

    return 0;
}

int
syncwerk_unset_inner_pub_repo (const char *repo_id, GError **error)
{
    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad args");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (syncw_repo_manager_unset_inner_pub_repo (syncw->repo_mgr, repo_id) < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Internal error");
        return -1;
    }

    return 0;
}

GList *
syncwerk_list_inner_pub_repos (GError **error)
{
    return syncw_repo_manager_list_inner_pub_repos (syncw->repo_mgr);
}

gint64
syncwerk_count_inner_pub_repos (GError **error)
{
    return syncw_repo_manager_count_inner_pub_repos (syncw->repo_mgr);
}

GList *
syncwerk_list_inner_pub_repos_by_owner (const char *user, GError **error)
{
    if (!user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Bad arguments");
        return NULL;
    }

    return syncw_repo_manager_list_inner_pub_repos_by_owner (syncw->repo_mgr, user);
}

int
syncwerk_is_inner_pub_repo (const char *repo_id, GError **error)
{
    if (!repo_id) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Bad arguments");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    return syncw_repo_manager_is_inner_pub_repo (syncw->repo_mgr, repo_id);
}

gint64
syncwerk_get_file_size (const char *store_id, int version,
                       const char *file_id, GError **error)
{
    gint64 file_size;

    if (!store_id || !is_uuid_valid(store_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid store id");
        return -1;
    }

    if (!file_id || !is_object_id_valid (file_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid file id");
        return -1;
    }

    file_size = syncw_fs_manager_get_file_size (syncw->fs_mgr, store_id, version, file_id);
    if (file_size < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "failed to read file size");
        return -1;
    }

    return file_size;
}

gint64
syncwerk_get_dir_size (const char *store_id, int version,
                      const char *dir_id, GError **error)
{
    gint64 dir_size;

    if (!store_id || !is_uuid_valid (store_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid store id");
        return -1;
    }

    if (!dir_id || !is_object_id_valid (dir_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid dir id");
        return -1;
    }

    dir_size = syncw_fs_manager_get_fs_size (syncw->fs_mgr, store_id, version, dir_id);
    if (dir_size < 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Failed to caculate dir size");
        return -1;
    }

    return dir_size;
}

int
syncwerk_check_passwd (const char *repo_id,
                      const char *magic,
                      GError **error)
{
    if (!repo_id || strlen(repo_id) != 36 || !magic) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return -1;
    }

    if (syncw_passwd_manager_check_passwd (syncw->passwd_mgr,
                                          repo_id, magic,
                                          error) < 0) {
        return -1;
    }

    return 0;
}

int
syncwerk_set_passwd (const char *repo_id,
                    const char *user,
                    const char *passwd,
                    GError **error)
{
    if (!repo_id || strlen(repo_id) != 36 || !user || !passwd) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return -1;
    }

    if (syncw_passwd_manager_set_passwd (syncw->passwd_mgr,
                                        repo_id, user, passwd,
                                        error) < 0) {
        return -1;
    }

    return 0;
}

int
syncwerk_unset_passwd (const char *repo_id,
                      const char *user,
                      GError **error)
{
    if (!repo_id || strlen(repo_id) != 36 || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return -1;
    }

    if (syncw_passwd_manager_unset_passwd (syncw->passwd_mgr,
                                          repo_id, user,
                                          error) < 0) {
        return -1;
    }

    return 0;
}

int
syncwerk_is_passwd_set (const char *repo_id, const char *user, GError **error)
{
    if (!repo_id || strlen(repo_id) != 36 || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return -1;
    }

    return syncw_passwd_manager_is_passwd_set (syncw->passwd_mgr,
                                              repo_id, user);
}

GObject *
syncwerk_get_decrypt_key (const char *repo_id, const char *user, GError **error)
{
    SyncwerkCryptKey *ret;

    if (!repo_id || strlen(repo_id) != 36 || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return NULL;
    }

    ret = syncw_passwd_manager_get_decrypt_key (syncw->passwd_mgr,
                                               repo_id, user);
    if (!ret) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Password was not set");
        return NULL;
    }

    return (GObject *)ret;
}

int
syncwerk_revert_on_server (const char *repo_id,
                          const char *commit_id,
                          const char *user_name,
                          GError **error)
{
    if (!repo_id || strlen(repo_id) != 36 ||
        !commit_id || strlen(commit_id) != 40 ||
        !user_name) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (!is_object_id_valid (commit_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid commit id");
        return -1;
    }

    return syncw_repo_manager_revert_on_server (syncw->repo_mgr,
                                               repo_id,
                                               commit_id,
                                               user_name,
                                               error);
}

int
syncwerk_post_file (const char *repo_id, const char *temp_file_path,
                   const char *parent_dir, const char *file_name,
                   const char *user,
                   GError **error)
{
    char *norm_parent_dir = NULL, *norm_file_name = NULL, *rpath = NULL;
    int ret = 0;

    if (!repo_id || !temp_file_path || !parent_dir || !file_name || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Argument should not be null");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    norm_parent_dir = normalize_utf8_path (parent_dir);
    if (!norm_parent_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    norm_file_name = normalize_utf8_path (file_name);
    if (!norm_file_name) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    rpath = format_dir_path (norm_parent_dir);

    if (syncw_repo_manager_post_file (syncw->repo_mgr, repo_id,
                                     temp_file_path, rpath,
                                     norm_file_name, user,
                                     error) < 0) {
        ret = -1;
    }

out:
    g_free (norm_parent_dir);
    g_free (norm_file_name);
    g_free (rpath);

    return ret;
}

/* char * */
/* syncwerk_post_file_blocks (const char *repo_id, */
/*                           const char *parent_dir, */
/*                           const char *file_name, */
/*                           const char *blockids_json, */
/*                           const char *paths_json, */
/*                           const char *user, */
/*                           gint64 file_size, */
/*                           int replace_existed, */
/*                           GError **error) */
/* { */
/*     char *norm_parent_dir = NULL, *norm_file_name = NULL, *rpath = NULL; */
/*     char *new_id = NULL; */

/*     if (!repo_id || !parent_dir || !file_name */
/*         || !blockids_json || ! paths_json || !user || file_size < 0) { */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Argument should not be null"); */
/*         return NULL; */
/*     } */

/*     if (!is_uuid_valid (repo_id)) { */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id"); */
/*         return NULL; */
/*     } */

/*     norm_parent_dir = normalize_utf8_path (parent_dir); */
/*     if (!norm_parent_dir) { */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Path is in valid UTF8 encoding"); */
/*         goto out; */
/*     } */

/*     norm_file_name = normalize_utf8_path (file_name); */
/*     if (!norm_file_name) { */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Path is in valid UTF8 encoding"); */
/*         goto out; */
/*     } */

/*     rpath = format_dir_path (norm_parent_dir); */

/*     syncw_repo_manager_post_file_blocks (syncw->repo_mgr, */
/*                                         repo_id, */
/*                                         rpath, */
/*                                         norm_file_name, */
/*                                         blockids_json, */
/*                                         paths_json, */
/*                                         user, */
/*                                         file_size, */
/*                                         replace_existed, */
/*                                         &new_id, */
/*                                         error); */

/* out: */
/*     g_free (norm_parent_dir); */
/*     g_free (norm_file_name); */
/*     g_free (rpath); */

/*     return new_id; */
/* } */

char *
syncwerk_post_multi_files (const char *repo_id,
                          const char *parent_dir,
                          const char *filenames_json,
                          const char *paths_json,
                          const char *user,
                          int replace_existed,
                          GError **error)
{
    char *norm_parent_dir = NULL, *rpath = NULL;
    char *ret_json = NULL;

    if (!repo_id || !filenames_json || !parent_dir || !paths_json || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Argument should not be null");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    norm_parent_dir = normalize_utf8_path (parent_dir);
    if (!norm_parent_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    rpath = format_dir_path (norm_parent_dir);

    syncw_repo_manager_post_multi_files (syncw->repo_mgr,
                                        repo_id,
                                        rpath,
                                        filenames_json,
                                        paths_json,
                                        user,
                                        replace_existed,
                                        &ret_json,
                                        NULL,
                                        error);

out:
    g_free (norm_parent_dir);
    g_free (rpath);

    return ret_json;
}

char *
syncwerk_put_file (const char *repo_id, const char *temp_file_path,
                  const char *parent_dir, const char *file_name,
                  const char *user, const char *head_id,
                  GError **error)
{
    char *norm_parent_dir = NULL, *norm_file_name = NULL, *rpath = NULL;
    char *new_file_id = NULL;

    if (!repo_id || !temp_file_path || !parent_dir || !file_name || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Argument should not be null");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    norm_parent_dir = normalize_utf8_path (parent_dir);
    if (!norm_parent_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    norm_file_name = normalize_utf8_path (file_name);
    if (!norm_file_name) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    rpath = format_dir_path (norm_parent_dir);

    syncw_repo_manager_put_file (syncw->repo_mgr, repo_id,
                                temp_file_path, rpath,
                                norm_file_name, user, head_id,
                                &new_file_id, error);

out:
    g_free (norm_parent_dir);
    g_free (norm_file_name);
    g_free (rpath);

    return new_file_id;
}

/* char * */
/* syncwerk_put_file_blocks (const char *repo_id, const char *parent_dir, */
/*                          const char *file_name, const char *blockids_json, */
/*                          const char *paths_json, const char *user, */
/*                          const char *head_id, gint64 file_size, GError **error) */
/* { */
/*     char *norm_parent_dir = NULL, *norm_file_name = NULL, *rpath = NULL; */
/*     char *new_file_id = NULL; */

/*     if (!repo_id || !parent_dir || !file_name */
/*         || !blockids_json || ! paths_json || !user) { */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Argument should not be null"); */
/*         return NULL; */
/*     } */

/*     if (!is_uuid_valid (repo_id)) { */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id"); */
/*         return NULL; */
/*     } */

/*     norm_parent_dir = normalize_utf8_path (parent_dir); */
/*     if (!norm_parent_dir) { */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Path is in valid UTF8 encoding"); */
/*         goto out; */
/*     } */

/*     norm_file_name = normalize_utf8_path (file_name); */
/*     if (!norm_file_name) { */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Path is in valid UTF8 encoding"); */
/*         goto out; */
/*     } */

/*     rpath = format_dir_path (norm_parent_dir); */

/*     syncw_repo_manager_put_file_blocks (syncw->repo_mgr, repo_id, */
/*                                        rpath, norm_file_name, */
/*                                        blockids_json, paths_json, */
/*                                        user, head_id, file_size, */
/*                                        &new_file_id, error); */

/* out: */
/*     g_free (norm_parent_dir); */
/*     g_free (norm_file_name); */
/*     g_free (rpath); */

/*     return new_file_id; */
/* } */

int
syncwerk_post_dir (const char *repo_id, const char *parent_dir,
                  const char *new_dir_name, const char *user,
                  GError **error)
{
    char *norm_parent_dir = NULL, *norm_dir_name = NULL, *rpath = NULL;
    int ret = 0;

    if (!repo_id || !parent_dir || !new_dir_name || !user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    norm_parent_dir = normalize_utf8_path (parent_dir);
    if (!norm_parent_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    norm_dir_name = normalize_utf8_path (new_dir_name);
    if (!norm_dir_name) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    rpath = format_dir_path (norm_parent_dir);

    if (syncw_repo_manager_post_dir (syncw->repo_mgr, repo_id,
                                    rpath, norm_dir_name,
                                    user, error) < 0) {
        ret = -1;
    }

out:
    g_free (norm_parent_dir);
    g_free (norm_dir_name);
    g_free (rpath);

    return ret;
}

int
syncwerk_post_empty_file (const char *repo_id, const char *parent_dir,
                         const char *new_file_name, const char *user,
                         GError **error)
{
    char *norm_parent_dir = NULL, *norm_file_name = NULL, *rpath = NULL;
    int ret = 0;

    if (!repo_id || !parent_dir || !new_file_name || !user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    norm_parent_dir = normalize_utf8_path (parent_dir);
    if (!norm_parent_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    norm_file_name = normalize_utf8_path (new_file_name);
    if (!norm_file_name) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    rpath = format_dir_path (norm_parent_dir);

    if (syncw_repo_manager_post_empty_file (syncw->repo_mgr, repo_id,
                                           rpath, norm_file_name,
                                           user, error) < 0) {
        ret = -1;
    }

out:
    g_free (norm_parent_dir);
    g_free (norm_file_name);
    g_free (rpath);

    return ret;
}

int
syncwerk_del_file (const char *repo_id, const char *parent_dir,
                  const char *file_name, const char *user,
                  GError **error)
{
    char *norm_parent_dir = NULL, *norm_file_name = NULL, *rpath = NULL;
    int ret = 0;

    if (!repo_id || !parent_dir || !file_name || !user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    norm_parent_dir = normalize_utf8_path (parent_dir);
    if (!norm_parent_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    norm_file_name = normalize_utf8_path (file_name);
    if (!norm_file_name) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    rpath = format_dir_path (norm_parent_dir);

    if (syncw_repo_manager_del_file (syncw->repo_mgr, repo_id,
                                    rpath, norm_file_name,
                                    user, error) < 0) {
        ret = -1;
    }

out:
    g_free (norm_parent_dir);
    g_free (norm_file_name);
    g_free (rpath);

    return ret;
}

GObject *
syncwerk_copy_file (const char *src_repo_id,
                   const char *src_dir,
                   const char *src_filename,
                   const char *dst_repo_id,
                   const char *dst_dir,
                   const char *dst_filename,
                   const char *user,
                   int need_progress,
                   int synchronous,
                   GError **error)
{
    char *norm_src_dir = NULL, *norm_src_filename = NULL;
    char *norm_dst_dir = NULL, *norm_dst_filename = NULL;
    char *rsrc_dir = NULL, *rdst_dir = NULL;
    GObject *ret = NULL;

    if (!src_repo_id || !src_dir || !src_filename ||
        !dst_repo_id || !dst_dir || !dst_filename || !user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    if (!is_uuid_valid (src_repo_id) || !is_uuid_valid(dst_repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    norm_src_dir = normalize_utf8_path (src_dir);
    if (!norm_src_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    norm_src_filename = normalize_utf8_path (src_filename);
    if (!norm_src_filename) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    norm_dst_dir = normalize_utf8_path (dst_dir);
    if (!norm_dst_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    norm_dst_filename = normalize_utf8_path (dst_filename);
    if (!norm_dst_filename) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    rsrc_dir = format_dir_path (norm_src_dir);
    rdst_dir = format_dir_path (norm_dst_dir);

    if (strchr(norm_src_filename, '\t') && strchr(norm_dst_filename, '\t'))
        ret = (GObject *)syncw_repo_manager_copy_multiple_files (syncw->repo_mgr,
                                                                src_repo_id, rsrc_dir, norm_src_filename,
                                                                dst_repo_id, rdst_dir, norm_dst_filename,
                                                                user, need_progress, synchronous,
                                                                error);

    else    
        ret = (GObject *)syncw_repo_manager_copy_file (syncw->repo_mgr,
                                                      src_repo_id, rsrc_dir, norm_src_filename,
                                                      dst_repo_id, rdst_dir, norm_dst_filename,
                                                      user, need_progress, synchronous,
                                                      error);

out:
    g_free (norm_src_dir);
    g_free (norm_src_filename);
    g_free (norm_dst_dir);
    g_free (norm_dst_filename);
    g_free (rsrc_dir);
    g_free (rdst_dir);

    return ret;
}

GObject *
syncwerk_move_file (const char *src_repo_id,
                   const char *src_dir,
                   const char *src_filename,
                   const char *dst_repo_id,
                   const char *dst_dir,
                   const char *dst_filename,
                   int replace,
                   const char *user,
                   int need_progress,
                   int synchronous,
                   GError **error)
{
    char *norm_src_dir = NULL, *norm_src_filename = NULL;
    char *norm_dst_dir = NULL, *norm_dst_filename = NULL;
    char *rsrc_dir = NULL, *rdst_dir = NULL;
    GObject *ret = NULL;

    if (!src_repo_id || !src_dir || !src_filename ||
        !dst_repo_id || !dst_dir || !dst_filename || !user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    if (!is_uuid_valid (src_repo_id) || !is_uuid_valid(dst_repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    norm_src_dir = normalize_utf8_path (src_dir);
    if (!norm_src_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    norm_src_filename = normalize_utf8_path (src_filename);
    if (!norm_src_filename) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    norm_dst_dir = normalize_utf8_path (dst_dir);
    if (!norm_dst_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    norm_dst_filename = normalize_utf8_path (dst_filename);
    if (!norm_dst_filename) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        goto out;
    }

    rsrc_dir = format_dir_path (norm_src_dir);
    rdst_dir = format_dir_path (norm_dst_dir);

    if (strchr(norm_src_filename, '\t') && strchr(norm_dst_filename, '\t'))
        ret = (GObject *)syncw_repo_manager_move_multiple_files (syncw->repo_mgr,
                                                                src_repo_id, rsrc_dir, norm_src_filename,
                                                                dst_repo_id, rdst_dir, norm_dst_filename,
                                                                replace, user, need_progress, synchronous,
                                                                error);
    else
        ret = (GObject *)syncw_repo_manager_move_file (syncw->repo_mgr,
                                                      src_repo_id, rsrc_dir, norm_src_filename,
                                                      dst_repo_id, rdst_dir, norm_dst_filename,
                                                      replace, user, need_progress, synchronous,
                                                      error);

out:
    g_free (norm_src_dir);
    g_free (norm_src_filename);
    g_free (norm_dst_dir);
    g_free (norm_dst_filename);
    g_free (rsrc_dir);
    g_free (rdst_dir);

    return ret;
}

GObject *
syncwerk_get_copy_task (const char *task_id, GError **error)
{
    return (GObject *)syncw_copy_manager_get_task (syncw->copy_mgr, task_id);
}

int
syncwerk_cancel_copy_task (const char *task_id, GError **error)
{
    return syncw_copy_manager_cancel_task (syncw->copy_mgr, task_id);
}

int
syncwerk_rename_file (const char *repo_id,
                     const char *parent_dir,
                     const char *oldname,
                     const char *newname,
                     const char *user,
                     GError **error)
{
    char *norm_parent_dir = NULL, *norm_oldname = NULL, *norm_newname = NULL;
    char *rpath = NULL;
    int ret = 0;

    if (!repo_id || !parent_dir || !oldname || !newname || !user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    norm_parent_dir = normalize_utf8_path (parent_dir);
    if (!norm_parent_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    norm_oldname = normalize_utf8_path (oldname);
    if (!norm_oldname) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    norm_newname = normalize_utf8_path (newname);
    if (!norm_newname) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Path is in valid UTF8 encoding");
        ret = -1;
        goto out;
    }

    rpath = format_dir_path (norm_parent_dir);

    if (syncw_repo_manager_rename_file (syncw->repo_mgr, repo_id,
                                       rpath, norm_oldname, norm_newname,
                                       user, error) < 0) {
        ret = -1;
    }

out:
    g_free (norm_parent_dir);
    g_free (norm_oldname);
    g_free (norm_newname);
    g_free (rpath);
    return ret;
}

int
syncwerk_is_valid_filename (const char *repo_id,
                           const char *filename,
                           GError **error)
{
    if (!repo_id || !filename) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    int ret = syncw_repo_manager_is_valid_filename (syncw->repo_mgr,
                                                   repo_id,
                                                   filename,
                                                   error);
    return ret;
}

char *
syncwerk_create_repo (const char *repo_name,
                     const char *repo_desc,
                     const char *owner_email,
                     const char *passwd,
                     GError **error)
{
    if (!repo_name || !repo_desc || !owner_email) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    char *repo_id;

    repo_id = syncw_repo_manager_create_new_repo (syncw->repo_mgr,
                                                 repo_name, repo_desc,
                                                 owner_email,
                                                 passwd,
                                                 error);
    return repo_id;
}

char *
syncwerk_create_enc_repo (const char *repo_id,
                         const char *repo_name,
                         const char *repo_desc,
                         const char *owner_email,
                         const char *magic,
                         const char *random_key,
                         int enc_version,
                         GError **error)
{
    if (!repo_id || !repo_name || !repo_desc || !owner_email) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    char *ret;

    ret = syncw_repo_manager_create_enc_repo (syncw->repo_mgr,
                                                 repo_id, repo_name, repo_desc,
                                                 owner_email,
                                                 magic, random_key, enc_version,
                                                 error);
    return ret;
}

int
syncwerk_set_user_quota (const char *user, gint64 quota, GError **error)
{
    if (!user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return -1;
    }

    return syncw_quota_manager_set_user_quota (syncw->quota_mgr, user, quota);
}

gint64
syncwerk_get_user_quota (const char *user, GError **error)
{
    if (!user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return -1;
    }

    return syncw_quota_manager_get_user_quota (syncw->quota_mgr, user);
}

int
syncwerk_check_quota (const char *repo_id, gint64 delta, GError **error)
{
    int rc;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad arguments");
        return -1;
    }

    rc = syncw_quota_manager_check_quota_with_delta (syncw->quota_mgr, repo_id, delta);
    if (rc == 1)
        return -1;
    return rc;
}

static char *
get_obj_id_by_path (const char *repo_id,
                    const char *path,
                    gboolean want_dir,
                    GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *commit = NULL;
    char *obj_id = NULL;

    if (!repo_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "Get repo error");
        goto out;
    }

    commit = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                             repo->id, repo->version,
                                             repo->head->commit_id);
    if (!commit) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "Get commit error");
        goto out;
    }

    guint32 mode = 0;
    obj_id = syncw_fs_manager_path_to_obj_id (syncw->fs_mgr,
                                             repo->store_id, repo->version,
                                             commit->root_id,
                                             path, &mode, error);

out:
    if (repo)
        syncw_repo_unref (repo);
    if (commit)
        syncw_commit_unref (commit);
    if (obj_id) {
        /* check if the mode matches */
        if ((want_dir && !S_ISDIR(mode)) || ((!want_dir) && S_ISDIR(mode))) {
            g_free (obj_id);
            return NULL;
        }
    }

    return obj_id;
}

char *syncwerk_get_file_id_by_path (const char *repo_id,
                                   const char *path,
                                   GError **error)
{
    char *rpath = format_dir_path (path);
    char *ret = get_obj_id_by_path (repo_id, rpath, FALSE, error);

    g_free (rpath);

    filter_error (error);

    return ret;
}

char *syncwerk_get_dir_id_by_path (const char *repo_id,
                                  const char *path,
                                  GError **error)
{
    char *rpath = format_dir_path (path);
    char *ret = get_obj_id_by_path (repo_id, rpath, TRUE, error);

    g_free (rpath);

    filter_error (error);

    return ret;
}

GObject *
syncwerk_get_dirent_by_path (const char *repo_id, const char *path,
                            GError **error)
{
    if (!repo_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "invalid repo id");
        return NULL;
    }

    char *rpath = format_dir_path (path);
    if (strcmp (rpath, "/") == 0) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "invalid path");
        g_free (rpath);
        return NULL;
    }

    SyncwRepo *repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "Get repo error");
        return NULL;
    }

    SyncwCommit *commit = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                                         repo->id, repo->version,
                                                         repo->head->commit_id);
    if (!commit) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "Get commit error");
        syncw_repo_unref (repo);
        return NULL;
    }

    SyncwDirent *dirent = syncw_fs_manager_get_dirent_by_path (syncw->fs_mgr,
                                                             repo->store_id, repo->version,
                                                             commit->root_id, rpath,
                                                             error);
    g_free (rpath);

    if (!dirent) {
        filter_error (error);
        syncw_repo_unref (repo);
        syncw_commit_unref (commit);
        return NULL;
    }

    GObject *obj = g_object_new (SYNCWERK_TYPE_DIRENT,
                                 "obj_id", dirent->id,
                                 "obj_name", dirent->name,
                                 "mode", dirent->mode,
                                 "version", dirent->version,
                                 "mtime", dirent->mtime,
                                 "size", dirent->size,
                                 "modifier", dirent->modifier,
                                 NULL);

    syncw_repo_unref (repo);
    syncw_commit_unref (commit);
    syncw_dirent_free (dirent);

    return obj;
}

char *
syncwerk_list_file_blocks (const char *repo_id,
                          const char *file_id,
                          int offset, int limit,
                          GError **error)
{
    SyncwRepo *repo;
    Syncwerk *file;
    GString *buf = g_string_new ("");
    int index = 0;

    if (!repo_id || !is_uuid_valid(repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_DIR_ID, "Bad repo id");
        return NULL;
    }

    if (!file_id || !is_object_id_valid(file_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_DIR_ID, "Bad file id");
        return NULL;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad repo id");
        return NULL;
    }

    file = syncw_fs_manager_get_syncwerk (syncw->fs_mgr,
                                        repo->store_id,
                                        repo->version, file_id);
    if (!file) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_DIR_ID, "Bad file id");
        syncw_repo_unref (repo);
        return NULL;
    }

    if (offset < 0)
        offset = 0;

    for (index = 0; index < file->n_blocks; index++) {
        if (index < offset) {
            continue;
        }

        if (limit > 0) {
            if (index >= offset + limit)
                break;
        }
        g_string_append_printf (buf, "%s\n", file->blk_sha1s[index]);
    }

    syncwerk_unref (file);
    syncw_repo_unref (repo);
    return g_string_free (buf, FALSE);
}

/*
 * Directories are always before files. Otherwise compare the names.
 */
static gint
comp_dirent_func (gconstpointer a, gconstpointer b)
{
    const SyncwDirent *dent_a = a, *dent_b = b;

    if (S_ISDIR(dent_a->mode) && S_ISREG(dent_b->mode))
        return -1;

    if (S_ISREG(dent_a->mode) && S_ISDIR(dent_b->mode))
        return 1;

    return strcasecmp (dent_a->name, dent_b->name);
}

GList *
syncwerk_list_dir (const char *repo_id,
                  const char *dir_id, int offset, int limit, GError **error)
{
    SyncwRepo *repo;
    SyncwDir *dir;
    SyncwDirent *dent;
    SyncwerkDirent *d;
    GList *res = NULL;
    GList *p;

    if (!repo_id || !is_uuid_valid(repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_DIR_ID, "Bad repo id");
        return NULL;
    }

    if (!dir_id || !is_object_id_valid (dir_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_DIR_ID, "Bad dir id");
        return NULL;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad repo id");
        return NULL;
    }

    dir = syncw_fs_manager_get_syncwdir (syncw->fs_mgr,
                                       repo->store_id, repo->version, dir_id);
    if (!dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_DIR_ID, "Bad dir id");
        syncw_repo_unref (repo);
        return NULL;
    }

    dir->entries = g_list_sort (dir->entries, comp_dirent_func);

    if (offset < 0) {
        offset = 0;
    }

    int index = 0;
    for (p = dir->entries; p != NULL; p = p->next, index++) {
        if (index < offset) {
            continue;
        }

        if (limit > 0) {
            if (index >= offset + limit)
                break;
        }

        dent = p->data;

        if (!is_object_id_valid (dent->id))
            continue;

        d = g_object_new (SYNCWERK_TYPE_DIRENT,
                          "obj_id", dent->id,
                          "obj_name", dent->name,
                          "mode", dent->mode,
                          "version", dent->version,
                          "mtime", dent->mtime,
                          "size", dent->size,
                          "permission", "",
                          NULL);
        res = g_list_prepend (res, d);
    }

    syncw_dir_free (dir);
    syncw_repo_unref (repo);
    res = g_list_reverse (res);
    return res;
}

GList *
syncwerk_list_file_revisions (const char *repo_id,
                             const char *commit_id,
                             const char *path,
                             int limit,
                             GError **error)
{
    if (!repo_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    char *rpath = format_dir_path (path);

    GList *commit_list;
    commit_list = syncw_repo_manager_list_file_revisions (syncw->repo_mgr,
                                                         repo_id, commit_id, rpath,
                                                         limit, FALSE, FALSE, error);
    g_free (rpath);

    return commit_list;
}

GList *
syncwerk_calc_files_last_modified (const char *repo_id,
                                  const char *parent_dir,
                                  int limit,
                                  GError **error)
{
    if (!repo_id || !parent_dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    char *rpath = format_dir_path (parent_dir);

    GList *ret = syncw_repo_manager_calc_files_last_modified (syncw->repo_mgr,
                                                             repo_id, rpath,
                                                             limit, error);
    g_free (rpath);

    return ret;
}

int
syncwerk_revert_file (const char *repo_id,
                     const char *commit_id,
                     const char *path,
                     const char *user,
                     GError **error)
{
    if (!repo_id || !commit_id || !path || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (!is_object_id_valid (commit_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid commit id");
        return -1;
    }

    char *rpath = format_dir_path (path);

    int ret = syncw_repo_manager_revert_file (syncw->repo_mgr,
                                             repo_id, commit_id,
                                             rpath, user, error);
    g_free (rpath);

    return ret;
}

int
syncwerk_revert_dir (const char *repo_id,
                    const char *commit_id,
                    const char *path,
                    const char *user,
                    GError **error)
{
    if (!repo_id || !commit_id || !path || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (!is_object_id_valid (commit_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid commit id");
        return -1;
    }

    char *rpath = format_dir_path (path);

    int ret = syncw_repo_manager_revert_dir (syncw->repo_mgr,
                                            repo_id, commit_id,
                                            rpath, user, error);
    g_free (rpath);

    return ret;
}


char *
syncwerk_check_repo_blocks_missing (const char *repo_id,
                                   const char *blockids_json,
                                   GError **error)
{
    json_t *array, *value, *ret_json;
    json_error_t err;
    size_t index;
    char *json_data, *ret;
    SyncwRepo *repo = NULL;

    array = json_loadb (blockids_json, strlen(blockids_json), 0, &err);
    if (!array) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return NULL;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        syncw_warning ("Failed to get repo %.8s.\n", repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Repo not found");
        json_decref (array);
        return NULL;
    }

    ret_json = json_array();
    size_t n = json_array_size (array);
    for (index = 0; index < n; index++) {
        value = json_array_get (array, index);
        const char *blockid = json_string_value (value);
        if (!blockid)
            continue;
        if (!syncw_block_manager_block_exists(syncw->block_mgr, repo_id,
                                             repo->version, blockid)) {
            json_array_append_new (ret_json, json_string(blockid));
        }
    }

    json_data = json_dumps (ret_json, 0);
    ret = g_strdup (json_data);

    free (json_data);
    json_decref (ret_json);
    json_decref (array);
    return ret;
}


GList *
syncwerk_get_deleted (const char *repo_id, int show_days,
                     const char *path, const char *scan_stat,
                     int limit, GError **error)
{
    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Bad arguments");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    char *rpath = NULL;
    if (path)
        rpath = format_dir_path (path);

    GList *ret = syncw_repo_manager_get_deleted_entries (syncw->repo_mgr,
                                                        repo_id, show_days,
                                                        rpath, scan_stat,
                                                        limit, error);
    g_free (rpath);

    return ret;
}

char *
syncwerk_generate_repo_token (const char *repo_id,
                             const char *email,
                             GError **error)
{
    char *token;

    if (!repo_id || !email) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    token = syncw_repo_manager_generate_repo_token (syncw->repo_mgr, repo_id, email, error);

    return token;
}

int
syncwerk_delete_repo_token (const char *repo_id,
                           const char *token,
                           const char *user,
                           GError **error)
{
    if (!repo_id || !token || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    return syncw_repo_manager_delete_token (syncw->repo_mgr,
                                           repo_id, token, user, error);
}

GList *
syncwerk_list_repo_tokens (const char *repo_id,
                          GError **error)
{
    GList *ret_list;

    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    ret_list = syncw_repo_manager_list_repo_tokens (syncw->repo_mgr, repo_id, error);

    return ret_list;
}

GList *
syncwerk_list_repo_tokens_by_email (const char *email,
                                   GError **error)
{
    GList *ret_list;

    if (!email) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return NULL;
    }

    ret_list = syncw_repo_manager_list_repo_tokens_by_email (syncw->repo_mgr, email, error);

    return ret_list;
}

int
syncwerk_delete_repo_tokens_by_peer_id(const char *email,
                                      const char *peer_id,
                                      GError **error)
{
    if (!email || !peer_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return -1;
    }

    /* check the peer id */
    if (strlen(peer_id) != 40) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "invalid peer id");
        return -1;
    }
    const char *c = peer_id;
    while (*c) {
        char v = *c;
        if ((v >= '0' && v <= '9') || (v >= 'a' && v <= 'z')) {
            c++;
            continue;
        } else {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "invalid peer id");
            return -1;
        }
    }

    GList *tokens = NULL;
    if (syncw_repo_manager_delete_repo_tokens_by_peer_id (syncw->repo_mgr, email, peer_id, &tokens, error) < 0) {
        g_list_free_full (tokens, (GDestroyNotify)g_free);
        return -1;
    }

    syncw_http_server_invalidate_tokens(syncw->http_server, tokens);
    g_list_free_full (tokens, (GDestroyNotify)g_free);
    return 0;
}

int
syncwerk_delete_repo_tokens_by_email (const char *email,
                                     GError **error)
{
    if (!email) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return -1;
    }

    return syncw_repo_manager_delete_repo_tokens_by_email (syncw->repo_mgr, email, error);
}

char *
syncwerk_check_permission (const char *repo_id, const char *user, GError **error)
{
    if (!repo_id || !user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    if (strlen(user) == 0)
        return NULL;

    return syncw_repo_manager_check_permission (syncw->repo_mgr,
                                               repo_id, user, error);
}

char *
syncwerk_check_permission_by_path (const char *repo_id, const char *path,
                                  const char *user, GError **error)
{
    return syncwerk_check_permission (repo_id, user, error);
}

GList *
syncwerk_list_dir_with_perm (const char *repo_id,
                            const char *path,
                            const char *dir_id,
                            const char *user,
                            int offset,
                            int limit,
                            GError **error)
{
    if (!repo_id || !is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    if (!dir_id || !is_object_id_valid (dir_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid dir id");
        return NULL;
    }

    char *rpath = format_dir_path (path);

    GList *ret = syncw_repo_manager_list_dir_with_perm (syncw->repo_mgr,
                                                       repo_id,
                                                       rpath,
                                                       dir_id,
                                                       user,
                                                       offset,
                                                       limit,
                                                       error);
    g_free (rpath);

    return ret;
}

int
syncwerk_set_share_permission (const char *repo_id,
                              const char *from_email,
                              const char *to_email,
                              const char *permission,
                              GError **error)
{
    if (!repo_id || !from_email || !to_email || !permission) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid repo_id parameter");
        return -1;
    }

    if (!is_permission_valid (permission)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid permission parameter");
        return -1;
    }

    return syncw_share_manager_set_permission (syncw->share_mgr,
                                              repo_id,
                                              from_email,
                                              to_email,
                                              permission);
}

int
syncwerk_set_group_repo_permission (int group_id,
                                   const char *repo_id,
                                   const char *permission,
                                   GError **error)
{
    if (!repo_id || !permission) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Arguments should not be empty");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (!is_permission_valid (permission)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid permission parameter");
        return -1;

    }

    return syncw_repo_manager_set_group_repo_perm (syncw->repo_mgr,
                                                  repo_id,
                                                  group_id,
                                                  permission,
                                                  error);
}

char *
syncwerk_get_file_id_by_commit_and_path(const char *repo_id,
                                       const char *commit_id,
                                       const char *path,
                                       GError **error)
{
    SyncwRepo *repo;
    SyncwCommit *commit;
    char *file_id;
    guint32 mode;

    if (!repo_id || !is_uuid_valid(repo_id) || !commit_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Arguments should not be empty");
        return NULL;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Bad repo id");
        return NULL;
    }

    commit = syncw_commit_manager_get_commit(syncw->commit_mgr,
                                            repo_id,
                                            repo->version,
                                            commit_id);
    if (!commit) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "bad commit id");
        syncw_repo_unref (repo);
        return NULL;
    }

    char *rpath = format_dir_path (path);

    file_id = syncw_fs_manager_path_to_obj_id (syncw->fs_mgr,
                                              repo->store_id, repo->version,
                                              commit->root_id, rpath, &mode, error);
    if (file_id && S_ISDIR(mode)) {
        g_free (file_id);
        file_id = NULL;
    }
    g_free (rpath);

    filter_error (error);

    syncw_commit_unref(commit);
    syncw_repo_unref (repo);

    return file_id;
}

/* Virtual repo related */

char *
syncwerk_create_virtual_repo (const char *origin_repo_id,
                             const char *path,
                             const char *repo_name,
                             const char *repo_desc,
                             const char *owner,
                             const char *passwd,
                             GError **error)
{
    if (!origin_repo_id || !path ||!repo_name || !repo_desc || !owner) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    if (!is_uuid_valid (origin_repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return NULL;
    }

    char *repo_id;
    char *rpath = format_dir_path (path);

    repo_id = syncw_repo_manager_create_virtual_repo (syncw->repo_mgr,
                                                     origin_repo_id, rpath,
                                                     repo_name, repo_desc,
                                                     owner, passwd, error);
    g_free (rpath);

    return repo_id;
}

GList *
syncwerk_get_virtual_repos_by_owner (const char *owner, GError **error)
{
    GList *repos, *ret = NULL, *ptr;
    SyncwRepo *r, *o;
    SyncwerkRepo *repo;
    char *orig_repo_id;
    gboolean is_original_owner;

    if (!owner) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    repos = syncw_repo_manager_get_virtual_repos_by_owner (syncw->repo_mgr,
                                                          owner,
                                                          error);
    for (ptr = repos; ptr != NULL; ptr = ptr->next) {
        r = ptr->data;

        orig_repo_id = r->virtual_info->origin_repo_id;
        o = syncw_repo_manager_get_repo (syncw->repo_mgr, orig_repo_id);
        if (!o) {
            syncw_warning ("Failed to get origin repo %.10s.\n", orig_repo_id);
            syncw_repo_unref (r);
            continue;
        }

        char *orig_owner = syncw_repo_manager_get_repo_owner (syncw->repo_mgr,
                                                             orig_repo_id);
        if (g_strcmp0 (orig_owner, owner) == 0)
            is_original_owner = TRUE;
        else
            is_original_owner = FALSE;
        g_free (orig_owner);

        char *perm = syncw_repo_manager_check_permission (syncw->repo_mgr,
                                                         r->id, owner, NULL);

        repo = (SyncwerkRepo *)convert_repo (r);
        if (repo) {
            g_object_set (repo, "is_original_owner", is_original_owner,
                          "origin_repo_name", o->name,
                          "virtual_perm", perm, NULL);
            ret = g_list_prepend (ret, repo);
        }

        syncw_repo_unref (r);
        syncw_repo_unref (o);
        g_free (perm);
    }
    g_list_free (repos);

    return g_list_reverse (ret);
}

GObject *
syncwerk_get_virtual_repo (const char *origin_repo,
                          const char *path,
                          const char *owner,
                          GError **error)
{
    char *repo_id;
    GObject *repo_obj;

    char *rpath = format_dir_path (path);

    repo_id = syncw_repo_manager_get_virtual_repo_id (syncw->repo_mgr,
                                                     origin_repo,
                                                     rpath,
                                                     owner);
    g_free (rpath);

    if (!repo_id)
        return NULL;

    repo_obj = syncwerk_get_repo (repo_id, error);

    g_free (repo_id);
    return repo_obj;
}

/* System default library */

char *
syncwerk_get_system_default_repo_id (GError **error)
{
    return get_system_default_repo_id(syncw);
}

static int
update_valid_since_time (SyncwRepo *repo, gint64 new_time)
{
    int ret = 0;
    gint64 old_time = syncw_repo_manager_get_repo_valid_since (repo->manager,
                                                              repo->id);

    if (new_time > 0) {
        if (new_time > old_time)
            ret = syncw_repo_manager_set_repo_valid_since (repo->manager,
                                                          repo->id,
                                                          new_time);
    } else if (new_time == 0) {
        /* Only the head commit is valid after GC if no history is kept. */
        SyncwCommit *head = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                                           repo->id, repo->version,
                                                           repo->head->commit_id);
        if (head && (old_time < 0 || head->ctime > (guint64)old_time))
            ret = syncw_repo_manager_set_repo_valid_since (repo->manager,
                                                          repo->id,
                                                          head->ctime);
        syncw_commit_unref (head);
    }

    return ret;
}

/* Clean up a repo's history.
 * It just set valid-since time but not actually delete the data.
 */
int
syncwerk_clean_up_repo_history (const char *repo_id, int keep_days, GError **error)
{
    SyncwRepo *repo;
    int ret;

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid arguments");
        return -1;
    }

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        syncw_warning ("Cannot find repo %s.\n", repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid arguments");
        return -1;
    }

    gint64 truncate_time, now;
    if (keep_days > 0) {
        now = (gint64)time(NULL);
        truncate_time = now - keep_days * 24 * 3600;
    } else
        truncate_time = 0;

    ret = update_valid_since_time (repo, truncate_time);
    if (ret < 0) {
        syncw_warning ("Failed to update valid since time for repo %.8s.\n", repo->id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Database error");
    }

    syncw_repo_unref (repo);
    return ret;
}

GList *
syncwerk_get_shared_users_for_subdir (const char *repo_id,
                                     const char *path,
                                     const char *from_user,
                                     GError **error)
{
    if (!repo_id || !path || !from_user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo_id");
        return NULL;
    }

    char *rpath = format_dir_path (path);

    GList *ret = syncw_repo_manager_get_shared_users_for_subdir (syncw->repo_mgr,
                                                                repo_id, rpath,
                                                                from_user, error);
    g_free (rpath);

    return ret;
}

GList *
syncwerk_get_shared_groups_for_subdir (const char *repo_id,
                                      const char *path,
                                      const char *from_user,
                                      GError **error)
{
    if (!repo_id || !path || !from_user) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo_id");
        return NULL;
    }

    char *rpath = format_dir_path (path);

    GList *ret = syncw_repo_manager_get_shared_groups_for_subdir (syncw->repo_mgr,
                                                                 repo_id, rpath,
                                                                 from_user, error);
    g_free (rpath);

    return ret;
}

gint64
syncwerk_get_total_file_number (GError **error)
{
    return syncw_get_total_file_number (error);
}

gint64
syncwerk_get_total_storage (GError **error)
{
    return syncw_get_total_storage (error);
}

GObject *
syncwerk_get_file_count_info_by_path (const char *repo_id,
                                     const char *path,
                                     GError **error)
{
    if (!repo_id || !path) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    GObject *ret = NULL;
    SyncwRepo *repo = NULL;
    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        syncw_warning ("Failed to get repo %.10s\n", repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Library not exists");
        return NULL;
    }

    ret = syncw_fs_manager_get_file_count_info_by_path (syncw->fs_mgr,
                                                       repo->store_id,
                                                       repo->version,
                                                       repo->root_id,
                                                       path, error);
    syncw_repo_unref (repo);

    return ret;
}

char *
syncwerk_get_trash_repo_owner (const char *repo_id, GError **error)
{
    if (!repo_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    return syncw_get_trash_repo_owner (repo_id);
}

int
syncwerk_mkdir_with_parents (const char *repo_id, const char *parent_dir,
                            const char *new_dir_path, const char *user,
                            GError **error)
{
    if (!repo_id || !parent_dir || !new_dir_path || !user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    if (!is_uuid_valid (repo_id)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo id");
        return -1;
    }

    if (syncw_repo_manager_mkdir_with_parents (syncw->repo_mgr, repo_id,
                                              parent_dir, new_dir_path,
                                              user, error) < 0) {
        return -1;
    }

    return 0;
}

int
syncwerk_set_server_config_int (const char *group, const char *key, int value,
                               GError **error)
{
    if (!group || !key) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    return syncw_cfg_manager_set_config_int (syncw->cfg_mgr, group, key, value);
}

int
syncwerk_get_server_config_int (const char *group, const char *key, GError **error)
{
    if (!group || !key ) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    return syncw_cfg_manager_get_config_int (syncw->cfg_mgr, group, key);
}

int
syncwerk_set_server_config_int64 (const char *group, const char *key, gint64 value,
                                 GError **error)
{
    if (!group || !key) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    return syncw_cfg_manager_set_config_int64 (syncw->cfg_mgr, group, key, value);
}

gint64
syncwerk_get_server_config_int64 (const char *group, const char *key, GError **error)
{
    if (!group || !key ) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    return syncw_cfg_manager_get_config_int64 (syncw->cfg_mgr, group, key);
}

int
syncwerk_set_server_config_string (const char *group, const char *key, const char *value,
                                  GError **error)
{
    if (!group || !key || !value) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    return syncw_cfg_manager_set_config_string (syncw->cfg_mgr, group, key, value);
}

char *
syncwerk_get_server_config_string (const char *group, const char *key, GError **error)
{
    if (!group || !key ) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return NULL;
    }

    return syncw_cfg_manager_get_config_string (syncw->cfg_mgr, group, key);
}

int
syncwerk_set_server_config_boolean (const char *group, const char *key, int value,
                                   GError **error)
{
    if (!group || !key) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    return syncw_cfg_manager_set_config_boolean (syncw->cfg_mgr, group, key, value);
}

int
syncwerk_get_server_config_boolean (const char *group, const char *key, GError **error)
{
    if (!group || !key ) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Argument should not be null");
        return -1;
    }

    return syncw_cfg_manager_get_config_boolean (syncw->cfg_mgr, group, key);
}

GObject *
syncwerk_get_group_shared_repo_by_path (const char *repo_id,
                                       const char *path,
                                       int group_id,
                                       int is_org,
                                       GError **error)
{
    if (!repo_id || group_id < 0) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Arguments error");
        return NULL;
    }
    SyncwRepoManager *mgr = syncw->repo_mgr;

    return syncw_get_group_shared_repo_by_path (mgr, repo_id, path, group_id, is_org ? TRUE:FALSE, error);
}

GObject *
syncwerk_get_shared_repo_by_path (const char *repo_id,
                                 const char *path,
                                 const char *shared_to,
                                 int is_org,
                                 GError **error)
{
    if (!repo_id || !shared_to) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Arguments error");
        return NULL;
    }
    SyncwRepoManager *mgr = syncw->repo_mgr;

    return syncw_get_shared_repo_by_path (mgr, repo_id, path, shared_to, is_org ? TRUE:FALSE, error);
}

GList *
syncwerk_get_group_repos_by_user (const char *user, GError **error)
{
    if (!user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Arguments error");
        return NULL;
    }
    SyncwRepoManager *mgr = syncw->repo_mgr;

    return syncw_get_group_repos_by_user (mgr, user, -1, error);
}

GList *
syncwerk_get_org_group_repos_by_user (const char *user, int org_id, GError **error)
{
    if (!user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Arguments error");
        return NULL;
    }
    SyncwRepoManager *mgr = syncw->repo_mgr;

    return syncw_get_group_repos_by_user (mgr, user, org_id, error);
}

int
syncwerk_repo_has_been_shared (const char *repo_id, int including_groups, GError **error)
{
    if (!repo_id) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Arguments error");
        return FALSE;
    }

    gboolean exists = syncw_share_manager_repo_has_been_shared (syncw->share_mgr, repo_id,
                                                               including_groups ? TRUE : FALSE);
    return exists ? 1 : 0;
}

GList *
syncwerk_get_shared_users_by_repo (const char *repo_id, GError **error)
{
    if (!repo_id) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Arguments error");
        return NULL;
    }

    return syncw_share_manager_get_shared_users_by_repo (syncw->share_mgr,
                                                        repo_id);
}

GList *
syncwerk_org_get_shared_users_by_repo (int org_id,
                                      const char *repo_id,
                                      GError **error)
{
    if (!repo_id || org_id < 0) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Arguments error");
        return NULL;
    }

    return syncw_share_manager_org_get_shared_users_by_repo (syncw->share_mgr,
                                                            org_id, repo_id);
}

char *
syncwerk_convert_repo_path (const char *repo_id,
                           const char *path,
                           const char *user,
                           int is_org,
                           GError **error)
{
    if (!is_uuid_valid(repo_id) || !path || !user) {
        g_set_error (error, 0, SYNCW_ERR_BAD_ARGS, "Arguments error");
        return NULL;
    }

    char *rpath = format_dir_path (path);
    char *ret = syncw_repo_manager_convert_repo_path(syncw->repo_mgr, repo_id, rpath, user, is_org ? TRUE : FALSE, error);
    g_free(rpath);

    return ret;
}
#endif  /* SYNCWERK_SERVER */
