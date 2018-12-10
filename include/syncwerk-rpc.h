
#ifndef _SYNCWERK_RPC_H
#define _SYNCWERK_RPC_H

#include "syncwerk-object.h"

/**
 * syncwerk_get_session_info:
 *
 * Returns: a SyncwerkSessionInfo object.
 */
GObject *
syncwerk_get_session_info (GError **error);

/**
 * syncwerk_get_repo_list:
 *
 * Returns repository list.
 */
GList* syncwerk_get_repo_list (int start, int limit, GError **error);

gint64
syncwerk_count_repos (GError **error);

/**
 * syncwerk_get_trash_repo_list:
 *
 * Returns deleted repository list.
 */
GList* syncwerk_get_trash_repo_list(int start, int limit, GError **error);

int
syncwerk_del_repo_from_trash (const char *repo_id, GError **error);

int
syncwerk_restore_repo_from_trash (const char *repo_id, GError **error);

GList *
syncwerk_get_trash_repos_by_owner (const char *owner, GError **error);

int
syncwerk_empty_repo_trash (GError **error);

int
syncwerk_empty_repo_trash_by_owner (const char *owner, GError **error);

/**
 * syncwerk_get_commit_list:
 *
 * @limit: if limit <= 0, all commits start from @offset will be returned.
 *
 * Returns: commit list of a given repo.
 *
 * Possible Error:
 *    1. Bad Argument
 *    2. No head and branch master
 *    3. Failed to list commits
 */
GList* syncwerk_get_commit_list (const gchar *repo,
                                int offset,
                                int limit,
                                GError **error);

/**
 * syncwerk_get_commit:
 * @id: the commit id.
 *
 * Returns: the commit object.
 */
GObject* syncwerk_get_commit (const char *repo_id, int version,
                             const gchar *id, GError **error);

/**
 * syncwerk_get_repo:
 *
 * Returns: repo
 */
GObject* syncwerk_get_repo (const gchar* id, GError **error);

GObject *
syncwerk_get_repo_sync_task (const char *repo_id, GError **error);

/**
 * syncwerk_get_repo_sync_info:
 */
GObject *
syncwerk_get_repo_sync_info (const char *repo_id, GError **error);

GList*
syncwerk_get_repo_sinfo (const char *repo_id, GError **error);

/* [syncwerk_get_config] returns the value of the config entry whose name is
 * [key] in config.db
 */
char *syncwerk_get_config (const char *key, GError **error);

/* [syncwerk_set_config] set the value of config key in config.db; old value
 * would be overwritten. */
int syncwerk_set_config (const char *key, const char *value, GError **error);

int
syncwerk_set_config_int (const char *key, int value, GError **error);

int
syncwerk_get_config_int (const char *key, GError **error);

int
syncwerk_set_upload_rate_limit (int limit, GError **error);

int
syncwerk_set_download_rate_limit (int limit, GError **error);

/**
 * syncwerk_destroy_repo:
 * @repo_id: repository id.
 */
int syncwerk_destroy_repo (const gchar *repo_id, GError **error);

int
syncwerk_unsync_repos_by_account (const char *server_addr, const char *email, GError **error);

int
syncwerk_remove_repo_tokens_by_account (const char *server_addr, const char *email, GError **error);

int
syncwerk_set_repo_token (const char *repo_id, const char *token, GError **error);

int
syncwerk_get_download_rate(GError **error);

int
syncwerk_get_upload_rate(GError **error);

/**
 * syncwerk_edit_repo:
 * @repo_id: repository id.
 * @name: new name of the repository, NULL if unchanged.
 * @description: new description of the repository, NULL if unchanged.
 */
int syncwerk_edit_repo (const gchar *repo_id, 
		       const gchar *name, 
		       const gchar *description,
                       const gchar *user,
		       GError **error);

int
syncwerk_change_repo_passwd (const char *repo_id,
                            const char *old_passwd,
                            const char *new_passwd,
                            const char *user,
                            GError **error);

/**
 * syncwerk_repo_size:
 * 
 * Returns: the size of a repo
 *
 * Possible Error:
 *   1. Bad Argument
 *   2. No local branch (No local branch record in branch.db)
 *   3. Database error
 *   4. Calculate branch size error
 */
gint64
syncwerk_repo_size(const gchar *repo_id, GError **error);

int
syncwerk_repo_last_modify(const char *repo_id, GError **error);

int syncwerk_set_repo_lantoken (const gchar *repo_id,
                               const gchar *token,
                               GError **error);

gchar* syncwerk_get_repo_lantoken (const gchar *repo_id,
                                  GError **error);

int
syncwerk_set_repo_property (const char *repo_id,
                           const char *key,
                           const char *value,
                           GError **error);

gchar *
syncwerk_get_repo_property (const char *repo_id,
                           const char *key,
                           GError **error);

char *
syncwerk_get_repo_relay_address (const char *repo_id,
                                GError **error);

char *
syncwerk_get_repo_relay_port (const char *repo_id,
                             GError **error);

int
syncwerk_update_repo_relay_info (const char *repo_id,
                                const char *new_addr,
                                const char *new_port,
                                GError **error);

int
syncwerk_update_repos_server_host (const char *old_host,
                                  const char *new_host,
                                  const char *new_server_url,
                                  GError **error);

int syncwerk_disable_auto_sync (GError **error);

int syncwerk_enable_auto_sync (GError **error);

int syncwerk_is_auto_sync_enabled (GError **error);

char *
syncwerk_get_path_sync_status (const char *repo_id,
                              const char *path,
                              int is_dir,
                              GError **error);

int
syncwerk_mark_file_locked (const char *repo_id, const char *path, GError **error);

int
syncwerk_mark_file_unlocked (const char *repo_id, const char *path, GError **error);

char *
syncwerk_get_server_property (const char *server_url, const char *key, GError **error);

int
syncwerk_set_server_property (const char *server_url,
                             const char *key,
                             const char *value,
                             GError **error);

/**
 * syncwerk_list_dir:
 * List a directory.
 *
 * Returns: a list of dirents.
 * 
 * @limit: if limit <= 0, all dirents start from @offset will be returned.
 */
GList * syncwerk_list_dir (const char *repo_id,
                          const char *dir_id, int offset, int limit, GError **error);

/**
 * syncwerk_list_file_blocks:
 * List the blocks of a file.
 *
 * Returns: a list of block ids speprated by '\n'.
 * 
 * @limit: if limit <= 0, all blocks start from @offset will be returned.
 */
char * syncwerk_list_file_blocks (const char *repo_id,
                                 const char *file_id,
                                 int offset, int limit,
                                 GError **error);

/**
 * syncwerk_list_dir_by_path:
 * List a directory in a commit by the path of the directory.
 *
 * Returns: a list of dirents.
 */
GList * syncwerk_list_dir_by_path (const char *repo_id,
                                  const char *commit_id, const char *path, GError **error);

/**
 * syncwerk_get_dir_id_by_commit_and_path:
 * Get the dir_id of the path
 *
 * Returns: the dir_id of the path
 */
char * syncwerk_get_dir_id_by_commit_and_path (const char *repo_id,
                                              const char *commit_id,
                                              const char *path,
                                              GError **error);

/**
 * syncwerk_revert:
 * Reset the repo to a previous state by creating a new commit.
 */
int syncwerk_revert (const char *repo_id, const char *commit, GError **error);

char *
syncwerk_gen_default_worktree (const char *worktree_parent,
                              const char *repo_name,
                              GError **error);
int
syncwerk_check_path_for_clone(const char *path, GError **error);

/**
 * syncwerk_clone:
 *
 * Fetch a new repo and then check it out.
 */
char *
syncwerk_clone (const char *repo_id, 
               int repo_version,
               const char *peer_id,
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
               GError **error);

char *
syncwerk_download (const char *repo_id, 
                  int repo_version,
                  const char *peer_id,
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
                  GError **error);

int
syncwerk_cancel_clone_task (const char *repo_id, GError **error);

int
syncwerk_remove_clone_task (const char *repo_id, GError **error);

/**
 * syncwerk_get_clone_tasks:
 *
 * Get a list of clone tasks.
 */
GList *
syncwerk_get_clone_tasks (GError **error);

/**
 * syncwerk_sync:
 *
 * Sync a repo with relay.
 */
int syncwerk_sync (const char *repo_id, const char *peer_id, GError **error);

/**
 * syncwerk_get_total_block_size:
 *
 * Get the sum of size of all the blocks.
 */
gint64
syncwerk_get_total_block_size (GError **error);


/**
 * syncwerk_get_commit_tree_block_number:
 *
 * Get the number of blocks belong to the commit tree.
 *
 * @commit_id: the head of the commit tree.
 *
 * Returns: -1 if the calculation is in progress, -2 if error, >=0 otherwise.
 */
int
syncwerk_get_commit_tree_block_number (const char *commit_id, GError **error);


/**
 * syncwerk_gc:
 * Start garbage collection.
 */
int
syncwerk_gc (GError **error);

/**
 * syncwerk_gc_get_progress:
 * Get progress of GC.
 *
 * Returns:
 *     progress of GC in precentage.
 *     -1 if GC is not running.
 */
/* int */
/* syncwerk_gc_get_progress (GError **error); */

/* -----------------  Task Related --------------  */

/**
 * syncwerk_find_transfer:
 *
 * Find a non finished task of a repo
 */
GObject *
syncwerk_find_transfer_task (const char *repo_id, GError *error);


int syncwerk_cancel_task (const gchar *task_id, int task_type, GError **error);

/**
 * Remove finished upload task
 */
int syncwerk_remove_task (const char *task_id, int task_type, GError **error);


/* ------------------ Relay specific RPC calls. ------------ */

/**
 * syncwerk_diff:
 *
 * Show the difference between @old commit and @new commit. If @old is NULL, then
 * show the difference between @new commit and its parent.
 *
 * @old and @new can also be branch name.
 */
GList *
syncwerk_diff (const char *repo_id, const char *old, const char *new,
              int fold_dir_results, GError **error);

GList *
syncwerk_branch_gets (const char *repo_id, GError **error);

/**
 * Return 1 if user is the owner of repo, otherwise return 0.
 */
int
syncwerk_is_repo_owner (const char *email, const char *repo_id,
                       GError **error);

int
syncwerk_set_repo_owner(const char *repo_id, const char *email,
                       GError **error);

/**
 * Return owner id of repo
 */
char *
syncwerk_get_repo_owner(const char *repo_id, GError **error);

GList *
syncwerk_get_orphan_repo_list(GError **error);

GList *
syncwerk_list_owned_repos (const char *email, int ret_corrupted, int start, int limit,
                          GError **error);

/**
 * syncwerk_add_chunk_server:
 * @server: ID for the chunk server.
 *
 * Add a chunk server on a relay server.
 */
int syncwerk_add_chunk_server (const char *server, GError **error);

/**
 * syncwerk_del_chunk_server:
 * @server: ID for the chunk server.
 *
 * Delete a chunk server on a relay server.
 */
int syncwerk_del_chunk_server (const char *server, GError **error);

/**
 * syncwerk_list_chunk_servers:
 *
 * List chunk servers set on a relay server.
 */
char *syncwerk_list_chunk_servers (GError **error);

gint64 syncwerk_get_user_quota_usage (const char *email, GError **error);

gint64 syncwerk_get_user_share_usage (const char *email, GError **error);

gint64
syncwerk_server_repo_size(const char *repo_id, GError **error);

int
syncwerk_repo_set_access_property (const char *repo_id, const char *ap,
                                  GError **error);

char *
syncwerk_repo_query_access_property (const char *repo_id, GError **error);

char *
syncwerk_web_get_access_token (const char *repo_id,
                              const char *obj_id,
                              const char *op,
                              const char *username,
                              int use_onetime,
                              GError **error);

GObject *
syncwerk_web_query_access_token (const char *token, GError **error);

char *
syncwerk_query_zip_progress (const char *token, GError **error);

int
syncwerk_cancel_zip_task (const char *token, GError **error);

GObject *
syncwerk_get_checkout_task (const char *repo_id, GError **error);

GList *
syncwerk_get_sync_task_list (GError **error);

char *
syncwerk_share_subdir_to_user (const char *repo_id,
                              const char *path,
                              const char *owner,
                              const char *share_user,
                              const char *permission,
                              const char *passwd,
                              GError **error);

int
syncwerk_unshare_subdir_for_user (const char *repo_id,
                                 const char *path,
                                 const char *owner,
                                 const char *share_user,
                                 GError **error);

int
syncwerk_update_share_subdir_perm_for_user (const char *repo_id,
                                           const char *path,
                                           const char *owner,
                                           const char *share_user,
                                           const char *permission,
                                           GError **error);

int
syncwerk_add_share (const char *repo_id, const char *from_email,
                   const char *to_email, const char *permission,
                   GError **error);

GList *
syncwerk_list_share_repos (const char *email, const char *type,
                          int start, int limit, GError **error);

GList *
syncwerk_list_repo_shared_to (const char *from_user, const char *repo_id,
                             GError **error);

GList *
syncwerk_list_repo_shared_group (const char *from_user, const char *repo_id,
                                GError **error);

int
syncwerk_remove_share (const char *repo_id, const char *from_email,
                      const char *to_email, GError **error);

char *
syncwerk_share_subdir_to_group (const char *repo_id,
                               const char *path,
                               const char *owner,
                               int share_group,
                               const char *permission,
                               const char *passwd,
                               GError **error);

int
syncwerk_unshare_subdir_for_group (const char *repo_id,
                                  const char *path,
                                  const char *owner,
                                  int share_group,
                                  GError **error);

int
syncwerk_update_share_subdir_perm_for_group (const char *repo_id,
                                            const char *path,
                                            const char *owner,
                                            int share_group,
                                            const char *permission,
                                            GError **error);

int
syncwerk_group_share_repo (const char *repo_id, int group_id,
                          const char *user_name, const char *permission,
                          GError **error);
int
syncwerk_group_unshare_repo (const char *repo_id, int group_id,
                            const char *user_name, GError **error);

/* Get groups that a repo is shared to */
char *
syncwerk_get_shared_groups_by_repo(const char *repo_id, GError **error);

char *
syncwerk_get_group_repoids (int group_id, GError **error);

GList *
syncwerk_get_repos_by_group (int group_id, GError **error);

GList *
syncwerk_get_group_repos_by_owner (char *user, GError **error);

char *
syncwerk_get_group_repo_owner (const char *repo_id, GError **error);

int
syncwerk_remove_repo_group(int group_id, const char *username, GError **error);

gint64
syncwerk_get_file_size (const char *store_id, int version,
                       const char *file_id, GError **error);

gint64
syncwerk_get_dir_size (const char *store_id, int version,
                      const char *dir_id, GError **error);

int
syncwerk_set_repo_history_limit (const char *repo_id,
                                int days,
                                GError **error);

int
syncwerk_get_repo_history_limit (const char *repo_id,
                                GError **error);

int
syncwerk_check_passwd (const char *repo_id,
                      const char *magic,
                      GError **error);

int
syncwerk_set_passwd (const char *repo_id,
                    const char *user,
                    const char *passwd,
                    GError **error);

int
syncwerk_unset_passwd (const char *repo_id,
                      const char *user,
                      GError **error);

int
syncwerk_is_passwd_set (const char *repo_id, const char *user, GError **error);

GObject *
syncwerk_get_decrypt_key (const char *repo_id, const char *user, GError **error);

int
syncwerk_revert_on_server (const char *repo_id,
                          const char *commit_id,
                          const char *user_name,
                          GError **error);

/**
 * Add a file into the repo on server.
 * The content of the file is stored in a temporary file.
 * @repo_id: repo id
 * @temp_file_path: local file path, should be a temp file just uploaded.
 * @parent_dir: the parent directory to put the file in.
 * @file_name: the name of the target file.
 * @user: the email of the user who uploaded the file.
 */
int
syncwerk_post_file (const char *repo_id, const char *temp_file_path,
                  const char *parent_dir, const char *file_name,
                  const char *user,
                  GError **error);

/**
 * Add multiple files at once.
 *
 * @filenames_json: json array of filenames
 * @paths_json: json array of temp file paths
 */
char *
syncwerk_post_multi_files (const char *repo_id,
                          const char *parent_dir,
                          const char *filenames_json,
                          const char *paths_json,
                          const char *user,
                          int replace,
                          GError **error);

/**
 * Add file blocks at once.
 *
 * @blocks_json: json array of block ids
 * @paths_json: json array of temp file paths
 */
/* char * */
/* syncwerk_post_file_blocks (const char *repo_id, */
/*                           const char *parent_dir, */
/*                           const char *file_name, */
/*                           const char *blockids_json, */
/*                           const char *paths_json, */
/*                           const char *user, */
/*                           gint64 file_size, */
/*                           int replace_existed, */
/*                           GError **error); */


int
syncwerk_post_empty_file (const char *repo_id, const char *parent_dir,
                         const char *new_file_name, const char *user,
                         GError **error);

/**
 * Update an existing file in a repo
 * @params: same as syncwerk_post_file
 * @head_id: the commit id for the original file version.
 *           It's optional. If it's NULL, the current repo head will be used.
 * @return The new file id
 */
char *
syncwerk_put_file (const char *repo_id, const char *temp_file_path,
                  const char *parent_dir, const char *file_name,
                  const char *user, const char *head_id,
                  GError **error);

/**
 * Add file blocks at once.
 *
 * @blocks_json: json array of block ids
 * @paths_json: json array of temp file paths
 */
/* char * */
/* syncwerk_put_file_blocks (const char *repo_id, const char *parent_dir, */
/*                          const char *file_name, const char *blockids_json, */
/*                          const char *paths_json, const char *user, */
/*                          const char *head_id, gint64 file_size, GError **error); */


int
syncwerk_post_dir (const char *repo_id, const char *parent_dir,
                  const char *new_dir_name, const char *user,
                  GError **error);
int
syncwerk_mkdir_with_parents (const char *repo_id, const char *parent_dir,
                            const char *new_dir_path, const char *user,
                            GError **error);

/**
 * delete a file/directory from the repo on server.
 * @repo_id: repo id
 * @parent_dir: the parent directory of the file to be deleted
 * @file_name: the name of the target file.
 * @user: the email of the user who uploaded the file.
 */
int
syncwerk_del_file (const char *repo_id, 
                  const char *parent_dir, const char *file_name,
                  const char *user,
                  GError **error);

/**
 * copy a file/directory from a repo to another on server.
 */
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
                   GError **error);


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
                   GError **error);

GObject *
syncwerk_get_copy_task (const char *task_id, GError **error);

int
syncwerk_cancel_copy_task (const char *task_id, GError **error);

int
syncwerk_rename_file (const char *repo_id,
                     const char *parent_dir,
                     const char *oldname,
                     const char *newname,
                     const char *user,
                     GError **error);

/**
 * Return non-zero if filename is valid.
 */
int
syncwerk_is_valid_filename (const char *repo_id,
                           const char *filename,
                           GError **error);


int
syncwerk_set_user_quota (const char *user, gint64 quota, GError **error);

gint64
syncwerk_get_user_quota (const char *user, GError **error);

int
syncwerk_check_quota (const char *repo_id, gint64 delta, GError **error);

char *
syncwerk_get_file_id_by_path (const char *repo_id, const char *path,
                             GError **error);

char *
syncwerk_get_dir_id_by_path (const char *repo_id, const char *path,
                            GError **error);

GObject *
syncwerk_get_dirent_by_path (const char *repo_id, const char *path,
                            GError **error);

/**
 * Return a list of commits where every commit contains a unique version of
 * the file.
 */
GList *
syncwerk_list_file_revisions (const char *repo_id,
                             const char *commit_id,
                             const char *path,
                             int limit,
                             GError **error);

GList *
syncwerk_calc_files_last_modified (const char *repo_id,
                                  const char *parent_dir,
                                  int limit,
                                  GError **error);

int
syncwerk_revert_file (const char *repo_id,
                     const char *commit_id,
                     const char *path,
                     const char *user,
                     GError **error);

int
syncwerk_revert_dir (const char *repo_id,
                    const char *commit_id,
                    const char *path,
                    const char *user,
                    GError **error);

char *
syncwerk_check_repo_blocks_missing (const char *repo_id,
                                   const char *blockids_json,
                                   GError **error);

/*
 * @show_days: return deleted files in how many days, return all if 0.
 */
GList *
syncwerk_get_deleted (const char *repo_id, int show_days,
                     const char *path, const char *scan_stat,
                     int limit, GError **error);

/**
 * Generate a new token for (repo_id, email) and return it
 */
char *
syncwerk_generate_repo_token (const char *repo_id,
                             const char *email,
                             GError **error);

int
syncwerk_delete_repo_token (const char *repo_id,
                           const char *token,
                           const char *user,
                           GError **error);

GList *
syncwerk_list_repo_tokens (const char *repo_id,
                          GError **error);

GList *
syncwerk_list_repo_tokens_by_email (const char *email,
                                   GError **error);

int
syncwerk_delete_repo_tokens_by_peer_id(const char *email, const char *peer_id, GError **error);

int
syncwerk_delete_repo_tokens_by_email (const char *email,
                                     GError **error);

/**
 * create a repo on restapi
 */
char *
syncwerk_create_repo (const char *repo_name,
                     const char *repo_desc,
                     const char *owner_email,
                     const char *passwd,
                     GError **error);

char *
syncwerk_create_enc_repo (const char *repo_id,
                         const char *repo_name,
                         const char *repo_desc,
                         const char *owner_email,
                         const char *magic,
                         const char *random_key,
                         int enc_version,
                         GError **error);

char *
syncwerk_check_permission (const char *repo_id, const char *user, GError **error);

char *
syncwerk_check_permission_by_path (const char *repo_id, const char *path,
                                  const char *user, GError **error);

GList *
syncwerk_list_dir_with_perm (const char *repo_id,
                            const char *path,
                            const char *dir_id,
                            const char *user,
                            int offset,
                            int limit,
                            GError **error);

int
syncwerk_set_inner_pub_repo (const char *repo_id,
                            const char *permission,
                            GError **error);

int
syncwerk_unset_inner_pub_repo (const char *repo_id, GError **error);

GList *
syncwerk_list_inner_pub_repos (GError **error);

gint64
syncwerk_count_inner_pub_repos (GError **error);

GList *
syncwerk_list_inner_pub_repos_by_owner (const char *user, GError **error);

int
syncwerk_is_inner_pub_repo (const char *repo_id, GError **error);

int
syncwerk_set_share_permission (const char *repo_id,
                              const char *from_email,
                              const char *to_email,
                              const char *permission,
                              GError **error);

int
syncwerk_set_group_repo_permission (int group_id,
                                   const char *repo_id,
                                   const char *permission,
                                   GError **error);

char *
syncwerk_get_file_id_by_commit_and_path(const char *repo_id,
                                       const char *commit_id,
                                       const char *path,
                                       GError **error);

/* virtual repo related */

char *
syncwerk_create_virtual_repo (const char *origin_repo_id,
                             const char *path,
                             const char *repo_name,
                             const char *repo_desc,
                             const char *owner,
                             const char *passwd,
                             GError **error);

GList *
syncwerk_get_virtual_repos_by_owner (const char *owner, GError **error);

GObject *
syncwerk_get_virtual_repo (const char *origin_repo,
                          const char *path,
                          const char *owner,
                          GError **error);

char *
syncwerk_get_system_default_repo_id (GError **error);

/* Clean trash */

int
syncwerk_clean_up_repo_history (const char *repo_id, int keep_days, GError **error);

/* ------------------ public RPC calls. ------------ */

GList* syncwerk_get_repo_list_pub (int start, int limit, GError **error);

GObject* syncwerk_get_repo_pub (const gchar* id, GError **error);

GList* syncwerk_get_commit_list_pub (const gchar *repo,
                                    int offset,
                                    int limit,
                                    GError **error);

GObject* syncwerk_get_commit_pub (const gchar *id, GError **error);

char *syncwerk_diff_pub (const char *repo_id, const char *old, const char *new,
                        GError **error);

GList * syncwerk_list_dir_pub (const char *dir_id, GError **error);

GList *
syncwerk_get_shared_users_for_subdir (const char *repo_id,
                                     const char *path,
                                     const char *from_user,
                                     GError **error);
GList *
syncwerk_get_shared_groups_for_subdir (const char *repo_id,
                                      const char *path,
                                      const char *from_user,
                                      GError **error);
GObject *
syncwerk_generate_magic_and_random_key(int enc_version,
                                      const char* repo_id,
                                      const char *passwd,
                                      GError **error);

gint64
syncwerk_get_total_file_number (GError **error);

gint64
syncwerk_get_total_storage (GError **error);

GObject *
syncwerk_get_file_count_info_by_path (const char *repo_id,
                                     const char *path,
                                     GError **error);

char *
syncwerk_get_trash_repo_owner (const char *repo_id, GError **error);

int
syncwerk_set_server_config_int (const char *group, const char *key, int value, GError **error);

int
syncwerk_get_server_config_int (const char *group, const char *key, GError **error);

int
syncwerk_set_server_config_int64 (const char *group, const char *key, gint64 value, GError **error);

gint64
syncwerk_get_server_config_int64 (const char *group, const char *key, GError **error);

int
syncwerk_set_server_config_string (const char *group, const char *key, const char *value, GError **error);

char *
syncwerk_get_server_config_string (const char *group, const char *key, GError **error);

int
syncwerk_set_server_config_boolean (const char *group, const char *key, int value, GError **error);

int
syncwerk_get_server_config_boolean (const char *group, const char *key, GError **error);

GObject *
syncwerk_get_group_shared_repo_by_path (const char *repo_id,
                                       const char *path,
                                       int group_id,
                                       int is_org,
                                       GError **error);

GObject *
syncwerk_get_shared_repo_by_path (const char *repo_id,
                                 const char *path,
                                 const char *shared_to,
                                 int is_org,
                                 GError **error);

GList *
syncwerk_get_group_repos_by_user (const char *user, GError **error);

GList *
syncwerk_get_org_group_repos_by_user (const char *user, int org_id, GError **error);

int
syncwerk_repo_has_been_shared (const char *repo_id, int including_groups, GError **error);

GList *
syncwerk_get_shared_users_by_repo (const char *repo_id, GError **error);

GList *
syncwerk_org_get_shared_users_by_repo (int org_id,
                                      const char *repo_id,
                                      GError **error);

char *
syncwerk_convert_repo_path (const char *repo_id,
                           const char *path,
                           const char *user,
                           int is_org,
                           GError **error);
#endif
