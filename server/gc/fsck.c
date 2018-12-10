#include "common.h"

#include <fcntl.h>

#include "syncwerk-session.h"
#include "log.h"
#include "utils.h"

#include "fsck.h"

typedef struct FsckData {
    gboolean repair;
    SyncwRepo *repo;
    GHashTable *existing_blocks;
    GList *repaired_files;
    GList *repaired_folders;
} FsckData;

typedef enum VerifyType {
    VERIFY_FILE,
    VERIFY_DIR
} VerifyType;

static gboolean
fsck_verify_objectstorage (const char *store_id,
                     int version,
                     const char *obj_id,
                     gboolean *io_error,
                     VerifyType type,
                     gboolean repair)
{
    gboolean valid = TRUE;

    valid = syncw_fs_manager_object_exists (syncw->fs_mgr, store_id,
                                           version, obj_id);
    if (!valid) {
        if (type == VERIFY_FILE) {
            syncw_message ("File %s is missing.\n", obj_id);
        }  else if (type == VERIFY_DIR) {
            syncw_message ("Dir %s is missing.\n", obj_id);
        }
        return valid;
    }

    if (type == VERIFY_FILE) {
        valid = syncw_fs_manager_verify_syncwerk (syncw->fs_mgr, store_id, version,
                                                obj_id, TRUE, io_error);
        if (!valid && !*io_error && repair) {
            syncw_message ("File %s is damaged, remove it.\n", obj_id);
            syncw_fs_manager_delete_object (syncw->fs_mgr, store_id, version, obj_id);
        }
    } else if (type == VERIFY_DIR) {
        valid = syncw_fs_manager_verify_syncwdir (syncw->fs_mgr, store_id, version,
                                                obj_id, TRUE, io_error);
        if (!valid && !*io_error && repair) {
            syncw_message ("Dir %s is damaged, remove it.\n", obj_id);
            syncw_fs_manager_delete_object (syncw->fs_mgr, store_id, version, obj_id);
        }
    }

    return valid;
}

static int
check_blocks (const char *file_id, FsckData *fsck_data, gboolean *io_error)
{
    Syncwerk *syncwerk;
    int i;
    char *block_id;
    int ret = 0;
    int dummy;

    gboolean ok = TRUE;
    SyncwRepo *repo = fsck_data->repo;
    const char *store_id = repo->store_id;
    int version = repo->version;

    syncwerk = syncw_fs_manager_get_syncwerk (syncw->fs_mgr, store_id,
                                           version, file_id);

    for (i = 0; i < syncwerk->n_blocks; ++i) {
        block_id = syncwerk->blk_sha1s[i];

        if (g_hash_table_lookup (fsck_data->existing_blocks, block_id))
            continue;

        if (!syncw_block_manager_block_exists (syncw->block_mgr,
                                              store_id, version,
                                              block_id)) {
            syncw_warning ("Block %s:%s is missing.\n", store_id, block_id);
            ret = -1;
            break;
        }

        // check block integrity, if not remove it
        ok = syncw_block_manager_verify_block (syncw->block_mgr,
                                              store_id, version,
                                              block_id, io_error);
        if (!ok) {
            if (*io_error) {
                ret = -1;
                break;
            } else {
                if (fsck_data->repair) {
                    syncw_message ("Block %s is damaged, remove it.\n", block_id);
                    syncw_block_manager_remove_block (syncw->block_mgr,
                                                     store_id, version,
                                                     block_id);
                } else {
                    syncw_message ("Block %s is damaged.\n", block_id);
                }
                ret = -1;
                break;
            }
        }

        g_hash_table_insert (fsck_data->existing_blocks, g_strdup(block_id), &dummy);
    }

    syncwerk_unref (syncwerk);

    return ret;
}

static char*
fsck_check_dir_recursive (const char *id, const char *parent_dir, FsckData *fsck_data)
{
    SyncwDir *dir;
    SyncwDir *new_dir;
    GList *p;
    SyncwDirent *syncw_dent;
    char *dir_id = NULL;
    char *path = NULL;
    gboolean io_error = FALSE;

    SyncwFSManager *mgr = syncw->fs_mgr;
    char *store_id = fsck_data->repo->store_id;
    int version = fsck_data->repo->version;
    gboolean is_corrupted = FALSE;

    dir = syncw_fs_manager_get_syncwdir (mgr, store_id, version, id);

    for (p = dir->entries; p; p = p->next) {
        syncw_dent = p->data;
        io_error = FALSE;

        if (S_ISREG(syncw_dent->mode)) {
            path = g_strdup_printf ("%s%s", parent_dir, syncw_dent->name);
            if (!path) {
                syncw_warning ("Out of memory, stop to run fsck for repo %.8s.\n",
                              fsck_data->repo->id);
                goto out;
            }
            if (!fsck_verify_objectstorage (store_id, version,
                                      syncw_dent->id, &io_error,
                                      VERIFY_FILE, fsck_data->repair)) {
                if (io_error) {
                    g_free (path);
                    goto out;
                }
                is_corrupted = TRUE;
                if (fsck_data->repair) {
                    syncw_message ("File %s(%.8s) is damaged, recreate an empty file.\n",
                                  path, syncw_dent->id);
                } else {
                    syncw_message ("File %s(%.8s) is damaged.\n",
                                  path, syncw_dent->id);
                }
                // file damaged, set it empty
                memcpy (syncw_dent->id, EMPTY_SHA1, 40);
                syncw_dent->mtime = (gint64)time(NULL);
                syncw_dent->size = 0;
            } else {
                if (check_blocks (syncw_dent->id, fsck_data, &io_error) < 0) {
                    if (io_error) {
                        g_free (path);
                        goto out;
                    }
                    is_corrupted = TRUE;
                    if (fsck_data->repair) {
                        syncw_message ("File %s(%.8s) is damaged, recreate an empty file.\n",
                                      path, syncw_dent->id);
                    } else {
                        syncw_message ("File %s(%.8s) is damaged.\n",
                                      path, syncw_dent->id);
                    }
                    // file damaged, set it empty
                    memcpy (syncw_dent->id, EMPTY_SHA1, 40);
                    syncw_dent->mtime = (gint64)time(NULL);
                    syncw_dent->size = 0;
                }
            }

            if (is_corrupted)
                fsck_data->repaired_files = g_list_prepend (fsck_data->repaired_files,
                                                            g_strdup(path));
            g_free (path);
        } else if (S_ISDIR(syncw_dent->mode)) {
            path = g_strdup_printf ("%s%s/", parent_dir, syncw_dent->name);
            if (!path) {
                syncw_warning ("Out of memory, stop to run fsck for repo %.8s.\n",
                              fsck_data->repo->id);
                goto out;
            }
            if (!fsck_verify_objectstorage (store_id, version,
                                      syncw_dent->id, &io_error,
                                      VERIFY_DIR, fsck_data->repair)) {
                if (io_error) {
                    g_free (path);
                    goto out;
                }
                if (fsck_data->repair) {
                    syncw_message ("Dir %s(%.8s) is damaged, recreate an empty dir.\n",
                                  path, syncw_dent->id);
                } else {
                    syncw_message ("Dir %s(%.8s) is damaged.\n",
                                  path, syncw_dent->id);
                }
                is_corrupted = TRUE;
                // dir damaged, set it empty
                memcpy (syncw_dent->id, EMPTY_SHA1, 40);

                fsck_data->repaired_folders = g_list_prepend (fsck_data->repaired_folders,
                                                              g_strdup(path));
            } else {
                dir_id = fsck_check_dir_recursive (syncw_dent->id, path, fsck_data);
                if (dir_id == NULL) {
                    // IO error
                    g_free (path);
                    goto out;
                }
                if (strcmp (dir_id, syncw_dent->id) != 0) {
                    is_corrupted = TRUE;
                    // dir damaged, set it to new dir_id
                    memcpy (syncw_dent->id, dir_id, 41);
                }
                g_free (dir_id);
            }
            g_free (path);
        }
    }

    if (is_corrupted) {
        new_dir = syncw_dir_new (NULL, dir->entries, version);
        if (fsck_data->repair) {
            if (syncw_dir_save (mgr, store_id, version, new_dir) < 0) {
                syncw_warning ("Failed to save dir\n");
                syncw_dir_free (new_dir);
                goto out;
            }
        }
        dir_id = g_strdup (new_dir->dir_id);
        syncw_dir_free (new_dir);
        dir->entries = NULL;
    } else {
        dir_id = g_strdup (dir->dir_id);
    }

out:
    syncw_dir_free (dir);

    return dir_id;
}

static gboolean
collect_token_list (SyncwDBRow *row, void *data)
{
    GList **p_tokens = data;
    const char *token;

    token = syncwerk_server_db_row_get_column_text (row, 0);
    *p_tokens = g_list_prepend (*p_tokens, g_strdup(token));

    return TRUE;
}

int
delete_repo_tokens (SyncwRepo *repo)
{
    int ret = 0;
    const char *template;
    GList *token_list = NULL;
    GList *ptr;
    GString *token_list_str = g_string_new ("");
    GString *sql = g_string_new ("");
    int rc;

    template = "SELECT u.token FROM RepoUserToken as u WHERE u.repo_id=?";
    rc = syncwerk_server_db_statement_foreach_row (syncw->db, template,
                                        collect_token_list, &token_list,
                                        1, "string", repo->id);
    if (rc < 0) {
        goto out;
    }

    if (rc == 0)
        goto out;

    for (ptr = token_list; ptr; ptr = ptr->next) {
        const char *token = (char *)ptr->data;
        if (token_list_str->len == 0)
            g_string_append_printf (token_list_str, "'%s'", token);
        else
            g_string_append_printf (token_list_str, ",'%s'", token);
    }

    /* Note that there is a size limit on sql query. In MySQL it's 1MB by default.
     * Normally the token_list won't be that long.
     */
    g_string_printf (sql, "DELETE FROM RepoUserToken WHERE token in (%s)",
                     token_list_str->str);
    rc = syncwerk_server_db_statement_query (syncw->db, sql->str, 0);
    if (rc < 0) {
        goto out;
    }

    g_string_printf (sql, "DELETE FROM RepoTokenPeerInfo WHERE token in (%s)",
                     token_list_str->str);
    rc = syncwerk_server_db_statement_query (syncw->db, sql->str, 0);
    if (rc < 0) {
        goto out;
    }

out:
    g_string_free (token_list_str, TRUE);
    g_string_free (sql, TRUE);
    g_list_free_full (token_list, (GDestroyNotify)g_free);

    if (rc < 0) {
        ret = -1;
    }

    return ret;
}

static char *
gen_repair_commit_desc (GList *repaired_files, GList *repaired_folders)
{
    GString *desc = g_string_new("Repaired by system.");
    GList *p;
    char *path;

    if (!repaired_files && !repaired_folders)
        return g_string_free (desc, FALSE);

    if (repaired_files) {
        g_string_append (desc, "\nDamaged files:\n");
        for (p = repaired_files; p; p = p->next) {
            path = p->data;
            g_string_append_printf (desc, "%s\n", path);
        }
    }

    if (repaired_folders) {
        g_string_append (desc, "\nDamaged folders:\n");
        for (p = repaired_folders; p; p = p->next) {
            path = p->data;
            g_string_append_printf (desc, "%s\n", path);
        }
    }

    return g_string_free (desc, FALSE);
}

static void
reset_commit_to_repair (SyncwRepo *repo, SyncwCommit *parent, char *new_root_id,
                        GList *repaired_files, GList *repaired_folders)
{
    if (delete_repo_tokens (repo) < 0) {
        syncw_warning ("Failed to delete repo sync tokens, abort repair.\n");
        return;
    }

    char *desc = gen_repair_commit_desc (repaired_files, repaired_folders);

    SyncwCommit *new_commit = NULL;
    new_commit = syncw_commit_new (NULL, repo->id, new_root_id,
                                  parent->creator_name, parent->creator_id,
                                  desc, 0);
    g_free (desc);
    if (!new_commit) {
        syncw_warning ("Out of memory, stop to run fsck for repo %.8s.\n",
                      repo->id);
        return;
    }

    new_commit->parent_id = g_strdup (parent->commit_id);
    syncw_repo_to_commit (repo, new_commit);

    syncw_message ("Update repo %.8s status to commit %.8s.\n",
                  repo->id, new_commit->commit_id);
    syncw_branch_set_commit (repo->head, new_commit->commit_id);
    if (syncw_branch_manager_add_branch (syncw->branch_mgr, repo->head) < 0) {
        syncw_warning ("Update head of repo %.8s to commit %.8s failed, "
                      "recover failed.\n", repo->id, new_commit->commit_id);
    } else {
        syncw_commit_manager_add_commit (syncw->commit_mgr, new_commit);
    }
    syncw_commit_unref (new_commit);
}

/*
 * check and recover repo, for damaged file or folder set it empty
 */
static void
check_and_recover_repo (SyncwRepo *repo, gboolean reset, gboolean repair)
{
    FsckData fsck_data;
    SyncwCommit *rep_commit = NULL;
    char *root_id = NULL;

    syncw_message ("Checking file system integrity of repo %s(%.8s)...\n",
                  repo->name, repo->id);

    rep_commit = syncw_commit_manager_get_commit (syncw->commit_mgr, repo->id,
                                                 repo->version, repo->head->commit_id);
    if (!rep_commit) {
        syncw_warning ("Failed to load commit %s of repo %s\n",
                      repo->head->commit_id, repo->id);
        return;
    }

    memset (&fsck_data, 0, sizeof(fsck_data));
    fsck_data.repair = repair;
    fsck_data.repo = repo;
    fsck_data.existing_blocks = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                       g_free, NULL);

    root_id = fsck_check_dir_recursive (rep_commit->root_id, "/", &fsck_data);
    g_hash_table_destroy (fsck_data.existing_blocks);
    if (root_id == NULL) {
        goto out;
    }

    if (repair) {
        if (strcmp (root_id, rep_commit->root_id) != 0) {
            // some fs objects damaged for the head commit,
            // create new head commit using the new root_id
            reset_commit_to_repair (repo, rep_commit, root_id,
                                    fsck_data.repaired_files,
                                    fsck_data.repaired_folders);
        } else if (reset) {
            // for reset commit but fs objects not damaged, also create a repaired commit
            reset_commit_to_repair (repo, rep_commit, rep_commit->root_id,
                                    NULL, NULL);
        }
    }

out:
    g_list_free_full (fsck_data.repaired_files, g_free);
    g_list_free_full (fsck_data.repaired_folders, g_free);
    g_free (root_id);
    syncw_commit_unref (rep_commit);
}

static gint
compare_commit_by_ctime (gconstpointer a, gconstpointer b)
{
    const SyncwCommit *commit_a = a;
    const SyncwCommit *commit_b = b;

    return (commit_b->ctime - commit_a->ctime);
}

static gboolean
fsck_get_repo_commit (const char *repo_id, int version,
                      const char *obj_id, void *commit_list)
{
    void *data = NULL;
    int data_len;
    GList **cur_list = (GList **)commit_list;

    int ret = syncw_obj_store_read_obj (syncw->commit_mgr->obj_store, repo_id,
                                       version, obj_id, &data, &data_len);
    if (ret < 0 || data == NULL)
        return TRUE;

    SyncwCommit *cur_commit = syncw_commit_from_data (obj_id, data, data_len);
    if (cur_commit != NULL) {
       *cur_list = g_list_prepend (*cur_list, cur_commit);
    }

    g_free(data);
    return TRUE;
}

static SyncwRepo*
get_available_repo (char *repo_id, gboolean repair)
{
    GList *commit_list = NULL;
    GList *temp_list = NULL;
    SyncwCommit *temp_commit = NULL;
    SyncwBranch *branch = NULL;
    SyncwRepo *repo = NULL;
    SyncwVirtRepo *vinfo = NULL;
    gboolean io_error;

    syncw_message ("Scanning available commits...\n");

    syncw_obj_store_foreach_obj (syncw->commit_mgr->obj_store, repo_id,
                                1, fsck_get_repo_commit, &commit_list);

    if (commit_list == NULL) {
        syncw_warning ("No available commits for repo %.8s, can't be repaired.\n",
                      repo_id);
        return NULL;
    }

    commit_list = g_list_sort (commit_list, compare_commit_by_ctime);

    repo = syncw_repo_new (repo_id, NULL, NULL);
    if (repo == NULL) {
        syncw_warning ("Out of memory, stop to run fsck for repo %.8s.\n",
                      repo_id);
        goto out;
    }

    vinfo = syncw_repo_manager_get_virtual_repo_info (syncw->repo_mgr, repo_id);
    if (vinfo) {
        repo->is_virtual = TRUE;
        memcpy (repo->store_id, vinfo->origin_repo_id, 36);
        syncw_virtual_repo_info_free (vinfo);
    } else {
        repo->is_virtual = FALSE;
        memcpy (repo->store_id, repo->id, 36);
    }

    for (temp_list = commit_list; temp_list; temp_list = temp_list->next) {
        temp_commit = temp_list->data;
        io_error = FALSE;

        if (!fsck_verify_objectstorage (repo->store_id, 1, temp_commit->root_id,
                                  &io_error, VERIFY_DIR, repair)) {
            if (io_error) {
                syncw_repo_unref (repo);
                repo = NULL;
                goto out;
            }
            // fs object of this commit is damaged,
            // continue to verify next
            continue;
        }

        branch = syncw_branch_new ("master", repo_id, temp_commit->commit_id);
        if (branch == NULL) {
            syncw_warning ("Out of memory, stop to run fsck for repo %.8s.\n",
                          repo_id);
            syncw_repo_unref (repo);
            repo = NULL;
            goto out;
        }
        repo->head = branch;
        syncw_repo_from_commit (repo, temp_commit);

        char time_buf[64];
        strftime (time_buf, 64, "%Y-%m-%d %H:%M:%S", localtime((time_t *)&temp_commit->ctime));
        syncw_message ("Find available commit %.8s(created at %s) for repo %.8s.\n",
                      temp_commit->commit_id, time_buf, repo_id);
        break;
    }

out:
    for (temp_list = commit_list; temp_list; temp_list = temp_list->next) {
        temp_commit = temp_list->data;
        syncw_commit_unref (temp_commit);
    }
    g_list_free (commit_list);

    if (!repo->head) {
        syncw_warning("No available commits found for repo %.8s, can't be repaired.\n",
                     repo_id);
        syncw_repo_unref (repo);
        return NULL;
    }

    return repo;
}

static void
repair_repos (GList *repo_id_list, gboolean repair)
{
    GList *ptr;
    char *repo_id;
    SyncwRepo *repo;
    gboolean exists;
    gboolean reset;
    gboolean io_error;

    for (ptr = repo_id_list; ptr; ptr = ptr->next) {
        reset = FALSE;
        repo_id = ptr->data;

        syncw_message ("Running fsck for repo %s.\n", repo_id);

        if (!is_uuid_valid (repo_id)) {
            syncw_warning ("Invalid repo id %s.\n", repo_id);
            goto next;
        }

        exists = syncw_repo_manager_repo_exists (syncw->repo_mgr, repo_id);
        if (!exists) {
            syncw_warning ("Repo %.8s doesn't exist.\n", repo_id);
            goto next;
        }

        repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);

        if (!repo) {
            syncw_message ("Repo %.8s HEAD commit is damaged, "
                          "need to restore to an old version.\n", repo_id);
            repo = get_available_repo (repo_id, repair);
            if (!repo) {
                goto next;
            }
            reset = TRUE;
        } else {
            SyncwCommit *commit = syncw_commit_manager_get_commit (syncw->commit_mgr, repo->id,
                                                                 repo->version,
                                                                 repo->head->commit_id);
            if (!commit) {
                syncw_warning ("Failed to get head commit %s of repo %s\n",
                              repo->head->commit_id, repo->id);
                syncw_repo_unref (repo);
                goto next;
            }

            io_error = FALSE;
            if (!fsck_verify_objectstorage (repo->store_id, repo->version,
                                      commit->root_id,  &io_error,
                                      VERIFY_DIR, repair)) {
                if (io_error) {
                    syncw_commit_unref (commit);
                    syncw_repo_unref (repo);
                    goto next;
                } else {
                    // root fs object is damaged, get available commit
                    syncw_message ("Repo %.8s HEAD commit is damaged, "
                                  "need to restore to an old version.\n", repo_id);
                    syncw_commit_unref (commit);
                    syncw_repo_unref (repo);
                    repo = get_available_repo (repo_id, repair);
                    if (!repo) {
                        goto next;
                    }
                    reset = TRUE;
                }
            } else {
                // head commit is available
                syncw_commit_unref (commit);
            }
        }

        check_and_recover_repo (repo, reset, repair);

        syncw_repo_unref (repo);
next:
        syncw_message ("Fsck finished for repo %.8s.\n\n", repo_id);
    }
}

int
syncwerk_server_fsck (GList *repo_id_list, gboolean repair)
{
    if (!repo_id_list)
        repo_id_list = syncw_repo_manager_get_repo_id_list (syncw->repo_mgr);

    repair_repos (repo_id_list, repair);

    while (repo_id_list) {
        g_free (repo_id_list->data);
        repo_id_list = g_list_delete_link (repo_id_list, repo_id_list);
    }

    return 0;
}

/* Export files. */

/*static gboolean
write_enc_block_to_file (const char *repo_id,
                         int version,
                         const char *block_id,
                         SyncwerkCrypt *crypt,
                         int fd,
                         const char *path)
{
    BlockHandle *handle;
    BlockMetadata *bmd;
    char buf[64 * 1024];
    int n;
    int remain;
    EVP_CIPHER_CTX ctx;
    char *dec_out;
    int dec_out_len;
    gboolean ret = TRUE;

    bmd = syncw_block_manager_stat_block (syncw->block_mgr,
                                         repo_id, version,
                                         block_id);
    if (!bmd) {
        syncw_warning ("Failed to stat block %s.\n", block_id);
        return FALSE;
    }

    handle = syncw_block_manager_open_block (syncw->block_mgr,
                                            repo_id, version,
                                            block_id, BLOCK_READ);
    if (!handle) {
        syncw_warning ("Failed to open block %s.\n", block_id);
        g_free (bmd);
        return FALSE;
    }

    if (syncwerk_decrypt_init (&ctx, crypt->version,
                              crypt->key, crypt->iv) < 0) {
        syncw_warning ("Failed to init decrypt.\n");
        ret = FALSE;
        goto out;
    }

    remain = bmd->size;
    while (1) {
        n = syncw_block_manager_read_block (syncw->block_mgr, handle, buf, sizeof(buf));
        if (n < 0) {
            syncw_warning ("Failed to read block %s.\n", block_id);
            ret = FALSE;
            break;
        } else if (n == 0) {
            break;
        }
        remain -= n;

        dec_out = g_new0 (char, n + 16);
        if (!dec_out) {
            syncw_warning ("Failed to alloc memory.\n");
            ret = FALSE;
            break;
        }

        if (EVP_DecryptUpdate (&ctx,
                               (unsigned char *)dec_out,
                               &dec_out_len,
                               (unsigned char *)buf,
                               n) == 0) {
            syncw_warning ("Failed to decrypt block %s .\n", block_id);
            g_free (dec_out);
            ret = FALSE;
            break;
        }

        if (writen (fd, dec_out, dec_out_len) != dec_out_len) {
            syncw_warning ("Failed to write block %s to file %s.\n",
                          block_id, path);
            g_free (dec_out);
            ret = FALSE;
            break;
        }

        if (remain == 0) {
            if (EVP_DecryptFinal_ex (&ctx,
                                     (unsigned char *)dec_out,
                                     &dec_out_len) == 0) {
                syncw_warning ("Failed to decrypt block %s .\n", block_id);
                g_free (dec_out);
                ret = FALSE;
                break;
            }
            if (dec_out_len > 0) {
                if (writen (fd, dec_out, dec_out_len) != dec_out_len) {
                    syncw_warning ("Failed to write block %s to file %s.\n",
                                  block_id, path);
                    g_free (dec_out);
                    ret = FALSE;
                    break;
                }
            }
        }

        g_free (dec_out);
    }

    EVP_CIPHER_CTX_cleanup (&ctx);

out:
    g_free (bmd);
    syncw_block_manager_close_block (syncw->block_mgr, handle);
    syncw_block_manager_block_handle_free (syncw->block_mgr, handle);

    return ret;
}*/

static gboolean
write_nonenc_block_to_file (const char *repo_id,
                            int version,
                            const char *block_id,
                            int fd,
                            const char *path)
{
    BlockHandle *handle;
    char buf[64 * 1024];
    gboolean ret = TRUE;
    int n;

    handle = syncw_block_manager_open_block (syncw->block_mgr,
                                            repo_id, version,
                                            block_id, BLOCK_READ);
    if (!handle) {
        return FALSE;
    }

    while (1) {
        n = syncw_block_manager_read_block (syncw->block_mgr, handle, buf, sizeof(buf));
        if (n < 0) {
            syncw_warning ("Failed to read block %s.\n", block_id);
            ret = FALSE;
            break;
        } else if (n == 0) {
            break;
        }

        if (writen (fd, buf, n) != n) {
            syncw_warning ("Failed to write block %s to file %s.\n",
                          block_id, path);
            ret = FALSE;
            break;
        }
    }

    syncw_block_manager_close_block (syncw->block_mgr, handle);
    syncw_block_manager_block_handle_free (syncw->block_mgr, handle);

    return ret;
}

static void
create_file (const char *repo_id,
             const char *file_id,
             const char *path)
{
    int i;
    char *block_id;
    int fd;
    Syncwerk *syncwerk;
    gboolean ret = TRUE;
    int version = 1;

    fd = g_open (path, O_CREAT | O_WRONLY | O_BINARY, 0666);
    if (fd < 0) {
        syncw_warning ("Open file %s failed: %s.\n", path, strerror (errno));
        return;
    }

    syncwerk = syncw_fs_manager_get_syncwerk (syncw->fs_mgr, repo_id,
                                           version, file_id);
    if (!syncwerk) {
        ret = FALSE;
        goto out;
    }

    for (i = 0; i < syncwerk->n_blocks; ++i) {
        block_id = syncwerk->blk_sha1s[i];

        ret = write_nonenc_block_to_file (repo_id, version, block_id,
                                          fd, path);
        if (!ret) {
            break;
        }
    }

out:
    close (fd);
    if (!ret) {
        if (g_unlink (path) < 0) {
            syncw_warning ("Failed to delete file %s: %s.\n", path, strerror (errno));
        }
        syncw_message ("Failed to export file %s.\n", path);
    } else {
        syncw_message ("Export file %s.\n", path);
    }
    syncwerk_unref (syncwerk);
}

static void
export_repo_files_recursive (const char *repo_id,
                             const char *id,
                             const char *parent_dir)
{
    SyncwDir *dir;
    GList *p;
    SyncwDirent *syncw_dent;
    char *path;

    SyncwFSManager *mgr = syncw->fs_mgr;
    int version = 1;

    dir = syncw_fs_manager_get_syncwdir (mgr, repo_id, version, id);
    if (!dir) {
        return;
    }

    for (p = dir->entries; p; p = p->next) {
        syncw_dent = p->data;
        path = g_build_filename (parent_dir, syncw_dent->name, NULL);

        if (S_ISREG(syncw_dent->mode)) {
            // create file
            create_file (repo_id, syncw_dent->id, path);
        } else if (S_ISDIR(syncw_dent->mode)) {
            if (g_mkdir (path, 0777) < 0) {
                syncw_warning ("Failed to mkdir %s: %s.\n", path,
                              strerror (errno));
                g_free (path);
                continue;
            } else {
                syncw_message ("Export dir %s.\n", path);
            }

            export_repo_files_recursive (repo_id, syncw_dent->id, path);
        }
        g_free (path);
    }

    syncw_dir_free (dir);
}

static SyncwCommit*
get_available_commit (const char *repo_id)
{
    GList *commit_list = NULL;
    GList *temp_list = NULL;
    GList *next_list = NULL;
    SyncwCommit *temp_commit = NULL;
    gboolean io_error;

    syncw_message ("Scanning available commits for repo %s...\n", repo_id);

    syncw_obj_store_foreach_obj (syncw->commit_mgr->obj_store, repo_id,
                                1, fsck_get_repo_commit, &commit_list);

    if (commit_list == NULL) {
        syncw_warning ("No available commits for repo %.8s, export failed.\n\n",
                      repo_id);
        return NULL;
    }

    commit_list = g_list_sort (commit_list, compare_commit_by_ctime);
    temp_list = commit_list;
    while (temp_list) {
        next_list = temp_list->next;
        temp_commit = temp_list->data;
        io_error = FALSE;

        if (memcmp (temp_commit->root_id, EMPTY_SHA1, 40) == 0) {
            syncw_commit_unref (temp_commit);
            temp_commit = NULL;
            temp_list = next_list;
            continue;
        } else if (!fsck_verify_objectstorage (repo_id, 1, temp_commit->root_id,
                                         &io_error, VERIFY_DIR, FALSE)) {
            syncw_commit_unref (temp_commit);
            temp_commit = NULL;
            temp_list = next_list;

            if (io_error) {
                break;
            }
            // fs object of this commit is damaged,
            // continue to verify next
            continue;
        }

        char time_buf[64];
        strftime (time_buf, 64, "%Y-%m-%d %H:%M:%S", localtime((time_t *)&temp_commit->ctime));
        syncw_message ("Find available commit %.8s(created at %s), will export files from it.\n",
                      temp_commit->commit_id, time_buf);
        temp_list = next_list;
        break;
    }

    while (temp_list) {
        syncw_commit_unref (temp_list->data);
        temp_list = temp_list->next;
    }
    g_list_free (commit_list);

    if (!temp_commit && !io_error) {
        syncw_warning ("No available commits for repo %.8s, export failed.\n\n",
                      repo_id);
    }

    return temp_commit;
}

void
export_repo_files (const char *repo_id,
                   const char *init_path,
                   GHashTable *enc_repos)
{
    SyncwCommit *commit = get_available_commit (repo_id);
    if (!commit) {
        return;
    }
    if (commit->encrypted) {
        g_hash_table_insert (enc_repos, g_strdup (repo_id),
                             g_strdup (commit->repo_name));
        syncw_commit_unref (commit);
        return;
    }

    syncw_message ("Start to export files for repo %.8s(%s).\n",
                  repo_id, commit->repo_name);

    char *dir_name = g_strdup_printf ("%.8s_%s_%s", repo_id,
                                      commit->repo_name,
                                      commit->creator_name);
    char * export_path = g_build_filename (init_path, dir_name, NULL);
    g_free (dir_name);
    if (g_mkdir (export_path, 0777) < 0) {
        syncw_warning ("Failed to create export dir %s: %s, export failed.\n",
                      export_path, strerror (errno));
        g_free (export_path);
        syncw_commit_unref (commit);
        return;
    }

    export_repo_files_recursive (repo_id, commit->root_id, export_path);

    syncw_message ("Finish exporting files for repo %.8s.\n\n", repo_id);

    g_free (export_path);
    syncw_commit_unref (commit);
}

static GList *
get_repo_ids (const char *syncwerk_dir)
{
    GList *repo_ids = NULL;
    char *commit_path = g_build_filename (syncwerk_dir, "storage",
                                          "commits", NULL);
    GError *error = NULL;

    GDir *dir = g_dir_open (commit_path, 0, &error);
    if (!dir) {
        syncw_warning ("Open dir %s failed: %s.\n",
                      commit_path, error->message);
        g_clear_error (&error);
        g_free (commit_path);
        return NULL;
    }

    const char *file_name;
    while ((file_name = g_dir_read_name (dir)) != NULL) {
        repo_ids = g_list_prepend (repo_ids, g_strdup (file_name));
    }
    g_dir_close (dir);

    g_free (commit_path);

    return repo_ids;
}

static void
print_enc_repo (gpointer key, gpointer value, gpointer user_data)
{
    syncw_message ("%s(%s)\n", (char *)key, (char *)value);
}

void
export_file (GList *repo_id_list, const char *syncwerk_dir, char *export_path)
{
    struct stat dir_st;

    if (stat (export_path, &dir_st) < 0) {
        if (errno == ENOENT) {
            if (g_mkdir (export_path, 0777) < 0) {
                syncw_warning ("Mkdir %s failed: %s.\n",
                              export_path, strerror (errno));
                return;
            }
        } else {
            syncw_warning ("Stat path: %s failed: %s.\n",
                          export_path, strerror (errno));
            return;
        }
    } else {
        if (!S_ISDIR(dir_st.st_mode)) {
            syncw_warning ("%s already exist, but it is not a directory.\n",
                          export_path);
            return;
        }
    }

    if (!repo_id_list) {
        repo_id_list = get_repo_ids (syncwerk_dir);
        if (!repo_id_list)
            return;
    }

    GList *iter = repo_id_list;
    char *repo_id;
    GHashTable *enc_repos = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, g_free);

    for (; iter; iter=iter->next) {
        repo_id = iter->data;
        if (!is_uuid_valid (repo_id)) {
            syncw_warning ("Invalid repo id: %s.\n", repo_id);
            continue;
        }

        export_repo_files (repo_id, export_path, enc_repos);
    }

    if (g_hash_table_size (enc_repos) > 0) {
        syncw_message ("The following repos are encrypted and are not exported:\n");
        g_hash_table_foreach (enc_repos, print_enc_repo, NULL);
    }

    while (repo_id_list) {
        g_free (repo_id_list->data);
        repo_id_list = g_list_delete_link (repo_id_list, repo_id_list);
    }
    g_hash_table_destroy (enc_repos);
    g_free (export_path);
}
