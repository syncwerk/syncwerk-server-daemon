/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <glib/gstdio.h>

#include <jansson.h>
#include <openssl/sha.h>

#include <ccnet.h>
#include <ccnet/ccnet-object.h>
#include "utils.h"
#define DEBUG_FLAG SYNCWERK_DEBUG_OTHER
#include "log.h"
#include "syncwerk-object.h"

#include "syncwerk-session.h"
#include "commit-mgr.h"
#include "branch-mgr.h"
#include "repo-mgr.h"
#include "fs-mgr.h"
#include "syncwerk-error.h"
#include "syncwerk-crypt.h"
#include "diff-simple.h"
#include "merge-new.h"

#include "syncwerk-server-db.h"

#define INDEX_DIR "index"

#define PREFIX_DEL_FILE "Deleted \""
#define PREFIX_DEL_DIR "Removed directory \""
#define PREFIX_DEL_DIRS "Removed \""

gboolean
should_ignore_file(const char *filename, void *data);

static gboolean
is_virtual_repo_and_origin (SyncwRepo *repo1, SyncwRepo *repo2);

int
post_files_and_gen_commit (GList *filenames,
                          SyncwRepo *repo,
                          const char *user,
                          char **ret_json,
                          int replace_existed,
                          const char *canon_path,
                          GList *id_list,
                          GList *size_list,
                          GError **error);

/*
 * Repo operations.
 */

static gint
compare_dirents (gconstpointer a, gconstpointer b)
{
    const SyncwDirent *ent_a = a, *ent_b = b;

    return strcmp (ent_b->name, ent_a->name);
}

static inline GList *
dup_syncwdir_entries (const GList *entries)
{
    const GList *p;
    GList *newentries = NULL;
    SyncwDirent *dent;
    
    for (p = entries; p; p = p->next) {
        dent = p->data;
        newentries = g_list_prepend (newentries, syncw_dirent_dup(dent));
    }

    return g_list_reverse(newentries);
}

static gboolean
filename_exists (GList *entries, const char *filename)
{
    GList *ptr;
    SyncwDirent *dent;

    for (ptr = entries; ptr != NULL; ptr = ptr->next) {
        dent = ptr->data;
        if (strcmp (dent->name, filename) == 0)
            return TRUE;
    }

    return FALSE;
}

static void
split_filename (const char *filename, char **name, char **ext)
{
    char *dot;

    dot = strrchr (filename, '.');
    if (dot) {
        *ext = g_strdup (dot + 1);
        *name = g_strndup (filename, dot - filename);
    } else {
        *name = g_strdup (filename);
        *ext = NULL;
    }
}

static char *
generate_unique_filename (const char *file, GList *entries)
{
    int i = 1;
    char *name, *ext, *unique_name;

    unique_name = g_strdup(file);
    split_filename (unique_name, &name, &ext);
    while (filename_exists (entries, unique_name) && i <= 100) {
        g_free (unique_name);
        if (ext)
            unique_name = g_strdup_printf ("%s (%d).%s", name, i, ext);
        else
            unique_name = g_strdup_printf ("%s (%d)", name, i);
        i++;
    }

    g_free (name);
    g_free (ext);

    if (i <= 100)
        return unique_name;
    else {
        g_free (unique_name);
        return NULL;
    }
}

/* We need to call this function recursively because every dirs in canon_path
 * need to be updated.
 */
static char *
post_file_recursive (SyncwRepo *repo,
                     const char *dir_id,
                     const char *to_path,
                     int replace_existed,
                     SyncwDirent *newdent)
{
    SyncwDir *olddir, *newdir;
    SyncwDirent *dent;
    GList *ptr;
    char *slash;
    char *to_path_dup = NULL;
    char *remain = NULL;
    char *id = NULL;
    char *ret = NULL;

    olddir = syncw_fs_manager_get_syncwdir_sorted(syncw->fs_mgr,
                                                repo->store_id, repo->version,
                                                dir_id);
    if (!olddir)
        return NULL;

    /* we reach the target dir.  new dir entry is added */
    if (*to_path == '\0') {
        GList *newentries = NULL;
        char *unique_name;
        SyncwDirent *dent_dup;
        if (replace_existed && filename_exists(olddir->entries, newdent->name)) {
            GList *p;
            SyncwDirent *dent;

            for (p = olddir->entries; p; p = p->next) {
                dent = p->data;
                if (strcmp(dent->name, newdent->name) == 0) {
                    newentries = g_list_prepend (newentries, syncw_dirent_dup(newdent));
                } else {
                    newentries = g_list_prepend (newentries, syncw_dirent_dup(dent));
                }
            }
            newentries = g_list_reverse (newentries);
            newdir = syncw_dir_new (NULL, newentries,
                                   dir_version_from_repo_version(repo->version));
            if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0) {
                ret = g_strdup (newdir->dir_id);
            }
            syncw_dir_free (newdir);
            goto out;
        }

        unique_name = generate_unique_filename (newdent->name, olddir->entries);
        if (!unique_name)
            goto out;
        dent_dup = syncw_dirent_new (newdent->version,
                                    newdent->id, newdent->mode, unique_name,
                                    newdent->mtime, newdent->modifier, newdent->size);
        g_free (unique_name);

        newentries = dup_syncwdir_entries (olddir->entries);

        newentries = g_list_insert_sorted (newentries,
                                           dent_dup,
                                           compare_dirents);

        newdir = syncw_dir_new (NULL, newentries,
                               dir_version_from_repo_version(repo->version));
        if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strdup (newdir->dir_id);
        syncw_dir_free (newdir);

        goto out;
    }

    to_path_dup = g_strdup (to_path);
    slash = strchr (to_path_dup, '/');

    if (!slash) {
        remain = to_path_dup + strlen(to_path_dup);
    } else {
        *slash = '\0';
        remain = slash + 1;
    }

    for (ptr = olddir->entries; ptr; ptr = ptr->next) {
        dent = (SyncwDirent *)ptr->data;

        if (strcmp(dent->name, to_path_dup) != 0)
            continue;

        id = post_file_recursive (repo, dent->id, remain, replace_existed, newdent);
        if (id != NULL) {
            memcpy(dent->id, id, 40);
            dent->id[40] = '\0';
            if (repo->version > 0)
                dent->mtime = (guint64)time(NULL);
        }
        break;
    }
    
    if (id != NULL) {
        /* Create a new SyncwDir. */
        GList *new_entries;
        
        new_entries = dup_syncwdir_entries (olddir->entries);
        newdir = syncw_dir_new (NULL, new_entries,
                               dir_version_from_repo_version(repo->version));
        if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strndup (newdir->dir_id, 40);
        syncw_dir_free (newdir);
    }

out:
    g_free (to_path_dup);
    g_free (id);
    syncw_dir_free(olddir);
    return ret;
}

static char *
do_post_file_replace (SyncwRepo *repo,
                      const char *root_id,
                      const char *parent_dir,
                      int replace_existed,
                      SyncwDirent *dent)
{
    /* if parent_dir is a absolutely path, we will remove the first '/' */
    if (*parent_dir == '/')
        parent_dir = parent_dir + 1;

    return post_file_recursive(repo, root_id, parent_dir, replace_existed, dent);
}

static char *
do_post_file (SyncwRepo *repo,
              const char *root_id,
              const char *parent_dir,
              SyncwDirent *dent)
{
    return do_post_file_replace(repo, root_id, parent_dir, 0, dent);
}

static char *
get_canonical_path (const char *path)
{
    char *ret = g_strdup (path);
    char *p;

    for (p = ret; *p != 0; ++p) {
        if (*p == '\\')
            *p = '/';
    }

    /* Remove trailing slashes from dir path. */
    int len = strlen(ret);
    int i = len - 1;
    while (i >= 0 && ret[i] == '/')
        ret[i--] = 0;

    return ret;
}

/* Return TRUE if @filename already existing in @parent_dir. If exists, and
   @mode is not NULL, set its value to the mode of the dirent.
*/
static gboolean
check_file_exists (const char *store_id,
                   int repo_version,
                   const char *root_id,
                   const char *parent_dir,
                   const char *filename,
                   int  *mode)
{
    SyncwDir *dir;
    GList *p;
    SyncwDirent *dent;
    int ret = FALSE;

    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               store_id, repo_version,
                                               root_id,
                                               parent_dir, NULL);
    if (!dir) {
        syncw_warning ("parent_dir %s doesn't exist in repo %s.\n",
                      parent_dir, store_id);
        return FALSE;
    }

    for (p = dir->entries; p != NULL; p = p->next) {
        dent = p->data;
        int r = strcmp (dent->name, filename);
        if (r == 0) {
            ret = TRUE;
            if (mode) {
                *mode = dent->mode;
            }
            break;
        }
    }

    syncw_dir_free (dir);

    return ret;
}

/**
  Various online file/directory operations:

  Put a file:
  1. find parent syncwdir
  2. add a new dirent to parent syncwdir
  2. recursively update all syncwdir in the path, in a bottom-up manner
  3. commit it

  Del a file/dir:
  basically the same as put a file

  copy a file/dir:
  1. get src dirent from src repo
  2. duplicate src dirent with the new file name
  3. put the new dirent to dst repo and commit it.

  Move a file/dir:
  basically the same as a copy operation. Just one more step:
  4. remove src dirent from src repo and commit it

  Rename a file/dir:
  1. find parent syncwdir
  2. update this syncwdir with the old dirent replaced by a new dirent.
  3. recursively update all syncwdir in the path
  
  NOTE:
  
  All operations which add a new dirent would check if a dirent with the same
  name already exists. If found, they would raise errors.

  All operations which remove a dirent would check if the dirent to be removed
  already exists. If not, they would do nothing and just return OK.

*/

#define GET_REPO_OR_FAIL(repo_var,repo_id)                              \
    do {                                                                \
        repo_var = syncw_repo_manager_get_repo (syncw->repo_mgr, (repo_id)); \
        if (!(repo_var)) {                                              \
            syncw_warning ("Repo %s doesn't exist.\n", (repo_id));       \
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid repo"); \
            ret = -1;                                                   \
            goto out;                                                   \
        }                                                               \
    } while (0);

#define GET_COMMIT_OR_FAIL(commit_var,repo_id,repo_version,commit_id)   \
    do {                                                                \
        commit_var = syncw_commit_manager_get_commit(syncw->commit_mgr, (repo_id), (repo_version), (commit_id)); \
        if (!(commit_var)) {                                            \
            syncw_warning ("commit %s:%s doesn't exist.\n", (repo_id), (commit_id)); \
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid commit"); \
            ret = -1;                                                   \
            goto out;                                                   \
        }                                                               \
    } while (0);

#define FAIL_IF_FILE_EXISTS(store_id,repo_version,root_id,parent_dir,filename,mode) \
    do {                                                                \
        if (check_file_exists ((store_id), (repo_version), (root_id), (parent_dir), (filename), (mode))) { \
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,      \
                         "file already exists");                        \
            ret = -1;                                                   \
            goto out;                                                   \
        }                                                               \
    } while (0);

#define FAIL_IF_FILE_NOT_EXISTS(store_id,repo_version,root_id,parent_dir,filename,mode)       \
    do {                                                                \
        if (!check_file_exists ((store_id), (repo_version), (root_id), (parent_dir), (filename), (mode))) { \
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,      \
                         "file does not exist");                        \
            ret = -1;                                                   \
            goto out;                                                   \
        }                                                               \
    } while (0);

#define STD_FILE_MODE (S_IFREG | 0644)

static char *
gen_merge_description (SyncwRepo *repo,
                       const char *merged_root,
                       const char *p1_root,
                       const char *p2_root)
{
    GList *p;
    GList *results = NULL;
    char *desc;
    
    diff_merge_roots (repo->store_id, repo->version,
                      merged_root, p1_root, p2_root, &results, TRUE);

    desc = diff_results_to_description (results);

    for (p = results; p; p = p->next) {
        DiffEntry *de = p->data;
        diff_entry_free (de);
    }
    g_list_free (results);

    return desc;
}

static int
gen_new_commit (const char *repo_id,
                SyncwCommit *base,
                const char *new_root,
                const char *user,
                const char *desc,
                char *new_commit_id,
                GError **error)
{
#define MAX_RETRY_COUNT 10

    SyncwRepo *repo = NULL;
    SyncwCommit *new_commit = NULL, *current_head = NULL, *merged_commit = NULL;
    int retry_cnt = 0;
    int ret = 0;

    repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
    if (!repo) {
        syncw_warning ("Repo %s doesn't exist.\n", repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Invalid repo");
        ret = -1;
        goto out;
    }

    /* Create a new commit pointing to new_root. */
    new_commit = syncw_commit_new(NULL, repo->id, new_root,
                                 user, EMPTY_SHA1,
                                 desc, 0);
    new_commit->parent_id = g_strdup (base->commit_id);
    syncw_repo_to_commit (repo, new_commit);

    if (syncw_commit_manager_add_commit (syncw->commit_mgr, new_commit) < 0) {
        syncw_warning ("Failed to add commit.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to add commit");
        ret = -1;
        goto out;
    }

retry:
    current_head = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                                   repo->id, repo->version, 
                                                   repo->head->commit_id);
    if (!current_head) {
        syncw_warning ("Failed to find head commit %s of %s.\n",
                      repo->head->commit_id, repo_id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Invalid repo");
        ret = -1;
        goto out;
    }

    /* Merge if base and head are not the same. */
    if (strcmp (base->commit_id, current_head->commit_id) != 0) {
        MergeOptions opt;
        const char *roots[3];
        char *desc = NULL;

        memset (&opt, 0, sizeof(opt));
        opt.n_ways = 3;
        memcpy (opt.remote_repo_id, repo_id, 36);
        memcpy (opt.remote_head, new_commit->commit_id, 40);
        opt.do_merge = TRUE;

        roots[0] = base->root_id; /* base */
        roots[1] = current_head->root_id; /* head */
        roots[2] = new_root;      /* remote */

        if (syncw_merge_trees (repo->store_id, repo->version, 3, roots, &opt) < 0) {
            syncw_warning ("Failed to merge.\n");
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "Internal error");
            ret = -1;
            goto out;
        }

        syncw_debug ("Number of dirs visted in merge %.8s: %d.\n",
                    repo_id, opt.visit_dirs);

        if (!opt.conflict)
            desc = g_strdup("Auto merge by system");
        else {
            desc = gen_merge_description (repo,
                                          opt.merged_tree_root,
                                          current_head->root_id,
                                          new_root);
            if (!desc)
                desc = g_strdup("Auto merge by system");
        }

        merged_commit = syncw_commit_new(NULL, repo->id, opt.merged_tree_root,
                                        user, EMPTY_SHA1,
                                        desc,
                                        0);
        g_free (desc);

        merged_commit->parent_id = g_strdup (current_head->commit_id);
        merged_commit->second_parent_id = g_strdup (new_commit->commit_id);
        merged_commit->new_merge = TRUE;
        if (opt.conflict)
            merged_commit->conflict = TRUE;
        syncw_repo_to_commit (repo, merged_commit);

        if (syncw_commit_manager_add_commit (syncw->commit_mgr, merged_commit) < 0) {
            syncw_warning ("Failed to add commit.\n");
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "Failed to add commit");
            ret = -1;
            goto out;
        }
    } else {
        syncw_commit_ref (new_commit);
        merged_commit = new_commit;
    }

    syncw_branch_set_commit(repo->head, merged_commit->commit_id);

    if (syncw_branch_manager_test_and_update_branch(syncw->branch_mgr,
                                                   repo->head,
                                                   current_head->commit_id) < 0)
    {
        syncw_repo_unref (repo);
        repo = NULL;
        syncw_commit_unref (current_head);
        current_head = NULL;
        syncw_commit_unref (merged_commit);
        merged_commit = NULL;

        if (++retry_cnt <= MAX_RETRY_COUNT) {
            /* Sleep random time between 100 and 1000 millisecs. */
            usleep (g_random_int_range(1, 11) * 100 * 1000);

            repo = syncw_repo_manager_get_repo (syncw->repo_mgr, repo_id);
            if (!repo) {
                syncw_warning ("Repo %s doesn't exist.\n", repo_id);
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Invalid repo");
                ret = -1;
                goto out;
            }

            goto retry;
        } else {
            syncw_warning ("Too many retries for concurrent update on repo %s. Stop retrying.\n",
                          repo_id);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, "Concurrent update");
            ret = -1;
            goto out;
        }
    }

    if (new_commit_id)
        memcpy (new_commit_id, merged_commit->commit_id, 41);

out:
    syncw_commit_unref (new_commit);
    syncw_commit_unref (current_head);
    syncw_commit_unref (merged_commit);
    syncw_repo_unref (repo);
    return ret;
}

static void
update_repo_size(const char *repo_id)
{
    schedule_repo_size_computation (syncw->size_sched, repo_id);
}

int
syncw_repo_manager_post_file (SyncwRepoManager *mgr,
                             const char *repo_id,
                             const char *temp_file_path,
                             const char *parent_dir,
                             const char *file_name,
                             const char *user,
                             GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char *canon_path = NULL;
    unsigned char sha1[20];
    char buf[SYNCW_PATH_MAX];
    char *root_id = NULL;
    SyncwerkCrypt *crypt = NULL;
    SyncwDirent *new_dent = NULL;
    char hex[41];
    int ret = 0;

    if (g_access (temp_file_path, R_OK) != 0) {
        syncw_warning ("[post file] File %s doesn't exist or not readable.\n",
                      temp_file_path);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid input file");
        return -1;
    }

    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    if (!canon_path)
        canon_path = get_canonical_path (parent_dir);

    if (should_ignore_file (file_name, NULL)) {
        syncw_debug ("[post file] Invalid filename %s.\n", file_name);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid filename");
        ret = -1;
        goto out;
    }

    if (strstr (parent_dir, "//") != NULL) {
        syncw_debug ("[post file] parent_dir cantains // sequence.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid parent dir");
        ret = -1;
        goto out;
    }
    
    /* Write blocks. */
    if (repo->encrypted) {
        unsigned char key[32], iv[16];
        if (syncw_passwd_manager_get_decrypt_key_raw (syncw->passwd_mgr,
                                                     repo_id, user,
                                                     key, iv) < 0) {
            syncw_debug ("Passwd for repo %s is not set.\n", repo_id);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "Passwd is not set");
            ret = -1;
            goto out;
        }
        crypt = syncwerk_crypt_new (repo->enc_version, key, iv);
    }

    gint64 size;
    if (syncw_fs_manager_index_blocks (syncw->fs_mgr,
                                      repo->store_id, repo->version,
                                      temp_file_path,
                                      sha1, &size, crypt, TRUE, FALSE, NULL) < 0) {
        syncw_warning ("failed to index blocks");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to index blocks");
        ret = -1;
        goto out;
    }

    rawdata_to_hex(sha1, hex, 20);
    new_dent = syncw_dirent_new (dir_version_from_repo_version (repo->version),
                                hex, STD_FILE_MODE, file_name,
                                (gint64)time(NULL), user, size);

    root_id = do_post_file (repo,
                            head_commit->root_id, canon_path, new_dent);
    if (!root_id) {
        syncw_warning ("[post file] Failed to post file %s to %s in repo %s.\n",
                      file_name, canon_path, repo->id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to post file");
        ret = -1;
        goto out;
    }

    snprintf(buf, SYNCW_PATH_MAX, "Added \"%s\"", file_name);
    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, buf, NULL, error) < 0) {
        ret = -1;
        goto out;
    }

    syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref(head_commit);
    syncw_dirent_free (new_dent);
    g_free (root_id);
    g_free (canon_path);
    g_free (crypt);

    if (ret == 0)
        update_repo_size(repo_id);

    return ret;
}

static int
add_new_entries (SyncwRepo *repo, const char *user, GList **entries,
                 GList *dents, int replace_existed, GList **name_list)
{
    GList *ptr;
    SyncwDirent *dent;

    for (ptr = dents; ptr; ptr = ptr->next) {
        dent = ptr->data;

        char *unique_name;
        SyncwDirent *newdent;
        gboolean replace = FALSE;

        if (replace_existed) {
            GList *p;
            SyncwDirent *tmp_dent;
            for (p = *entries; p; p = p->next) {
                tmp_dent = p->data;
                if (strcmp(tmp_dent->name, dent->name) == 0) {
                    replace = TRUE;
                    *entries = g_list_delete_link (*entries, p);
                    syncw_dirent_free (tmp_dent);
                    break;
                }
            }
        }

        if (replace)
            unique_name = g_strdup (dent->name);
        else
            unique_name = generate_unique_filename (dent->name, *entries);

        if (unique_name != NULL) {
            newdent = syncw_dirent_new (dir_version_from_repo_version(repo->version),
                                       dent->id, dent->mode, unique_name,
                                       (gint64)time(NULL), user, dent->size);
            *entries = g_list_insert_sorted (*entries, newdent, compare_dirents);
            *name_list = g_list_append (*name_list, unique_name);
            /* No need to free unique_name */
        } else {
            return -1;
        }
    }

    return 0;
}

static char *
post_multi_files_recursive (SyncwRepo *repo,
                            const char *dir_id,
                            const char *to_path,
                            GList *dents,
                            const char *user,
                            int replace_existed,
                            GList **name_list)
{
    SyncwDir *olddir, *newdir;
    SyncwDirent *dent;
    GList *ptr;
    char *slash;
    char *to_path_dup = NULL;
    char *remain = NULL;
    char *id = NULL;
    char *ret = NULL;

    olddir = syncw_fs_manager_get_syncwdir_sorted(syncw->fs_mgr,
                                                repo->store_id,
                                                repo->version,
                                                dir_id);
    if (!olddir)
        return NULL;

    /* we reach the target dir.  new dir entry is added */
    if (*to_path == '\0') {
        GList *newentries;

        newentries = dup_syncwdir_entries (olddir->entries);

        if (add_new_entries (repo, user,
                             &newentries, dents, replace_existed, name_list) < 0)
            goto out;

        newdir = syncw_dir_new (NULL, newentries,
                               dir_version_from_repo_version(repo->version));
        if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strdup (newdir->dir_id);
        syncw_dir_free (newdir);

        goto out;
    }

    to_path_dup = g_strdup (to_path);
    slash = strchr (to_path_dup, '/');

    if (!slash) {
        remain = to_path_dup + strlen(to_path_dup);
    } else {
        *slash = '\0';
        remain = slash + 1;
    }

    for (ptr = olddir->entries; ptr; ptr = ptr->next) {
        dent = (SyncwDirent *)ptr->data;

        if (strcmp(dent->name, to_path_dup) != 0)
            continue;

        id = post_multi_files_recursive (repo, dent->id, remain, dents, user,
                                         replace_existed, name_list);
        if (id != NULL) {
            memcpy(dent->id, id, 40);
            dent->id[40] = '\0';
            if (repo->version > 0)
                dent->mtime = (guint64)time(NULL);
        }
        break;
    }
    
    if (id != NULL) {
        /* Create a new SyncwDir. */
        GList *new_entries;
        
        new_entries = dup_syncwdir_entries (olddir->entries);
        newdir = syncw_dir_new (NULL, new_entries,
                               dir_version_from_repo_version(repo->version));
        if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strdup (newdir->dir_id);
        syncw_dir_free (newdir);
    }

out:
    g_free (to_path_dup);
    g_free (id);
    syncw_dir_free(olddir);
    return ret;
}

static char *
do_post_multi_files (SyncwRepo *repo,
                     const char *root_id,
                     const char *parent_dir,
                     GList *filenames,
                     GList *id_list,
                     GList *size_list,
                     const char *user,
                     int replace_existed,
                     GList **name_list)
{
    SyncwDirent *dent;
    GList *dents = NULL;
    GList *ptr1, *ptr2, *ptr3;
    char *ret;

    for (ptr1 = filenames, ptr2 = id_list, ptr3 = size_list;
         ptr1 && ptr2 && ptr3;
         ptr1 = ptr1->next, ptr2 = ptr2->next, ptr3 = ptr3->next) {

        char *name = ptr1->data;
        char *id = ptr2->data;
        gint64 *size = ptr3->data;

        dent = g_new0 (SyncwDirent, 1);
        dent->name = name;
        memcpy(dent->id, id, 40);
        dent->id[40] = '\0';
        dent->size = *size;
        dent->mode = STD_FILE_MODE;

        dents = g_list_append (dents, dent);
    }
    /* if parent_dir is a absolutely path, we will remove the first '/' */
    if (*parent_dir == '/')
        parent_dir = parent_dir + 1;

    ret = post_multi_files_recursive(repo, root_id, parent_dir,
                                     dents, user, replace_existed, name_list);
    g_list_free_full (dents, g_free);

    return ret;
}

static GList *
json_to_file_list (const char *files_json)
{
    json_t *array;
    GList *files = NULL;
    json_error_t jerror;
    size_t index;
    json_t *value;
    const char *file;
    char *norm_file;

    array = json_loadb (files_json, strlen(files_json), 0, &jerror);
    if (!array) {
        syncw_warning ("Failed to load json file list: %s.\n", jerror.text);
        return NULL;
    }

    size_t n = json_array_size (array);
    for (index = 0; index < n; index++) {
        value = json_array_get (array, index);
        file = json_string_value (value);
        if (!file) {
            g_list_free_full (files, g_free);
            files = NULL;
            break;
        }

        norm_file = normalize_utf8_path (file);
        if (!norm_file) {
            g_list_free_full (files, g_free);
            files = NULL;
            break;
        }

        files = g_list_prepend (files, norm_file);
    }

    json_decref (array);
    return g_list_reverse(files);
}

/*
 * Return [{'name': 'file1', 'id': 'id1', 'size': num1}, {'name': 'file2', 'id': 'id2', 'size': num2}]
 */
static char *
format_json_ret (GList *name_list, GList *id_list, GList *size_list)
{
    json_t *array, *obj;
    GList *ptr, *ptr2;
    GList *sptr;
    char *filename, *id;
    gint64 *size;
    char *json_data;
    char *ret;

    array = json_array ();

    for (ptr = name_list, ptr2 = id_list, sptr = size_list;
         ptr && ptr2 && sptr;
         ptr = ptr->next, ptr2 = ptr2->next, sptr = sptr->next) {
        filename = ptr->data;
        id = ptr2->data;
        size = sptr->data;
        obj = json_object ();
        json_object_set_string_member (obj, "name", filename);
        json_object_set_string_member (obj, "id", id);
        json_object_set_int_member (obj, "size", *size);
        json_array_append_new (array, obj);
    }

    json_data = json_dumps (array, 0);
    json_decref (array);

    ret = g_strdup (json_data);
    free (json_data);
    return ret;
}

int
syncw_repo_manager_post_multi_files (SyncwRepoManager *mgr,
                                    const char *repo_id,
                                    const char *parent_dir,
                                    const char *filenames_json,
                                    const char *paths_json,
                                    const char *user,
                                    int replace_existed,
                                    char **ret_json,
                                    char **task_id,
                                    GError **error)
{
    SyncwRepo *repo = NULL;
    char *canon_path = NULL;
    GList *filenames = NULL, *paths = NULL, *id_list = NULL, *size_list = NULL, *ptr;
    char *filename, *path;
    unsigned char sha1[20];
    SyncwerkCrypt *crypt = NULL;
    char hex[41];
    int ret = 0;

    GET_REPO_OR_FAIL(repo, repo_id);

    canon_path = get_canonical_path (parent_dir);

    /* Decode file name and tmp file paths from json. */
    filenames = json_to_file_list (filenames_json);
    paths = json_to_file_list (paths_json);
    if (!filenames || !paths) {
        syncw_debug ("[post files] Invalid filenames or paths.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid files");
        ret = -1;
        goto out;
    }

    /* Check inputs. */
    for (ptr = filenames; ptr; ptr = ptr->next) {
        filename = ptr->data;
        if (should_ignore_file (filename, NULL)) {
            syncw_debug ("[post files] Invalid filename %s.\n", filename);
            g_set_error (error, SYNCWERK_DOMAIN, POST_FILE_ERR_FILENAME,
                         "%s", filename);
            ret = -1;
            goto out;
        }
    }

    if (strstr (parent_dir, "//") != NULL) {
        syncw_debug ("[post file] parent_dir cantains // sequence.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid parent dir");
        ret = -1;
        goto out;
    }

    /* Index tmp files and get file id list. */
    if (repo->encrypted) {
        unsigned char key[32], iv[16];
        if (syncw_passwd_manager_get_decrypt_key_raw (syncw->passwd_mgr,
                                                     repo_id, user,
                                                     key, iv) < 0) {
            syncw_debug ("Passwd for repo %s is not set.\n", repo_id);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "Passwd is not set");
            ret = -1;
            goto out;
        }
        crypt = syncwerk_crypt_new (repo->enc_version, key, iv);
    }

    if (!task_id) {
        gint64 *size;
        for (ptr = paths; ptr; ptr = ptr->next) {
            path = ptr->data;

            size = g_new (gint64, 1);
            if (syncw_fs_manager_index_blocks (syncw->fs_mgr,
                                              repo->store_id, repo->version,
                                              path, sha1, size, crypt, TRUE, FALSE, NULL) < 0) {
                syncw_warning ("failed to index blocks");
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                             "Failed to index blocks");
                ret = -1;
                goto out;
            }

            rawdata_to_hex(sha1, hex, 20);
            id_list = g_list_prepend (id_list, g_strdup(hex));
            size_list = g_list_prepend (size_list, size);
        }
        id_list = g_list_reverse (id_list);
        size_list = g_list_reverse (size_list);

        ret = post_files_and_gen_commit (filenames,
                                         repo,
                                         user,
                                         ret_json,
                                         replace_existed,
                                         canon_path,
                                         id_list,
                                         size_list,
                                         error);
    } else {
        ret = index_blocks_mgr_start_index (syncw->index_blocks_mgr,
                                            filenames,
                                            paths,
                                            repo_id,
                                            user,
                                            replace_existed,
                                            ret_json == NULL ? FALSE : TRUE,
                                            canon_path,
                                            crypt,
                                            task_id);
    }

out:
    if (repo)
        syncw_repo_unref (repo);
    string_list_free (filenames);
    string_list_free (paths);
    string_list_free (id_list);
    for (ptr = size_list; ptr; ptr = ptr->next)
        g_free (ptr->data);
    g_list_free (size_list);
    g_free (canon_path);
    g_free (crypt);

    return ret;
}

int
post_files_and_gen_commit (GList *filenames,
                           SyncwRepo *repo,
                           const char *user,
                           char **ret_json,
                           int replace_existed,
                           const char *canon_path,
                           GList *id_list,
                           GList *size_list,
                           GError **error)
{
    GList *name_list = NULL;
    GString *buf = g_string_new (NULL);
    SyncwCommit *head_commit = NULL;
    char *root_id = NULL;
    int ret = 0;

    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    /* Add the files to parent dir and commit. */
    root_id = do_post_multi_files (repo, head_commit->root_id, canon_path,
                                   filenames, id_list, size_list, user,
                                   replace_existed, &name_list);
    if (!root_id) {
        syncw_warning ("[post multi-file] Failed to post files to %s in repo %s.\n",
                      canon_path, repo->id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "Failed to put file");
        ret = -1;
        goto out;
    }
    guint len = g_list_length (filenames);
    if (len > 1)
        g_string_printf (buf, "Added \"%s\" and %u more files.",
                         (char *)(filenames->data), len - 1);
    else
        g_string_printf (buf, "Added \"%s\".", (char *)(filenames->data));

    if (gen_new_commit (repo->id, head_commit, root_id,
                        user, buf->str, NULL, error) < 0) {
        ret = -1;
        goto out;
    }

    syncw_repo_manager_merge_virtual_repo (syncw->repo_mgr, repo->id, NULL);

    if (ret_json)
        *ret_json = format_json_ret (name_list, id_list, size_list);

    update_repo_size(repo->id);

out:
    if (head_commit)
        syncw_commit_unref(head_commit);
    string_list_free (name_list);
    g_string_free (buf, TRUE);
    g_free (root_id);

    return ret;
}

/* int */
/* syncw_repo_manager_post_file_blocks (SyncwRepoManager *mgr, */
/*                                     const char *repo_id, */
/*                                     const char *parent_dir, */
/*                                     const char *file_name, */
/*                                     const char *blockids_json, */
/*                                     const char *paths_json, */
/*                                     const char *user, */
/*                                     gint64 file_size, */
/*                                     int replace_existed, */
/*                                     char **new_id, */
/*                                     GError **error) */
/* { */
/*     SyncwRepo *repo = NULL; */
/*     SyncwCommit *head_commit = NULL; */
/*     char *canon_path = NULL; */
/*     unsigned char sha1[20]; */
/*     char buf[SYNCW_PATH_MAX]; */
/*     char *root_id = NULL; */
/*     SyncwDirent *new_dent = NULL; */
/*     GList *blockids = NULL, *paths = NULL, *ptr; */
/*     char hex[41]; */
/*     int ret = 0; */

/*     blockids = json_to_file_list (blockids_json); */
/*     paths = json_to_file_list (paths_json); */
/*     if (g_list_length(blockids) != g_list_length(paths)) { */
/*         syncw_debug ("[post-blks] Invalid blockids or paths.\n"); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid files"); */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     for (ptr = paths; ptr; ptr = ptr->next) { */
/*         char *temp_file_path = ptr->data; */
/*         if (g_access (temp_file_path, R_OK) != 0) { */
/*             syncw_warning ("[post-blks] File block %s doesn't exist or not readable.\n", */
/*                           temp_file_path); */
/*             g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                          "Invalid input file"); */
/*             ret = -1; */
/*             goto out; */
/*         } */
/*     } */

/*     GET_REPO_OR_FAIL(repo, repo_id); */
/*     GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id); */

/*     if (!canon_path) */
/*         canon_path = get_canonical_path (parent_dir); */

/*     if (should_ignore_file (file_name, NULL)) { */
/*         syncw_debug ("[post-blks] Invalid filename %s.\n", file_name); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Invalid filename"); */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     if (strstr (parent_dir, "//") != NULL) { */
/*         syncw_debug ("[post-blks] parent_dir cantains // sequence.\n"); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Invalid parent dir"); */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     /\* Write blocks. *\/ */
/*     if (syncw_fs_manager_index_file_blocks (syncw->fs_mgr, */
/*                                            repo->store_id, repo->version, */
/*                                            paths, */
/*                                            blockids, sha1, file_size) < 0) { */
/*         syncw_warning ("Failed to index file blocks"); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, */
/*                      "Failed to index blocks"); */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     rawdata_to_hex(sha1, hex, 20); */
/*     new_dent = syncw_dirent_new (dir_version_from_repo_version(repo->version), */
/*                                 hex, STD_FILE_MODE, file_name, */
/*                                 (gint64)time(NULL), user, file_size); */

/*     root_id = do_post_file_replace (repo, head_commit->root_id, */
/*                                     canon_path, replace_existed, new_dent); */
/*     if (!root_id) { */
/*         syncw_warning ("[post-blks] Failed to post file to %s in repo %s.\n", */
/*                       canon_path, repo->id); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, */
/*                      "Failed to put file"); */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     *new_id = g_strdup(hex); */
/*     snprintf(buf, SYNCW_PATH_MAX, "Added \"%s\"", file_name); */
/*     if (gen_new_commit (repo_id, head_commit, root_id, */
/*                         user, buf, NULL, error) < 0) */
/*         ret = -1; */

/* out: */
/*     if (repo) */
/*         syncw_repo_unref (repo); */
/*     if (head_commit) */
/*         syncw_commit_unref(head_commit); */
/*     string_list_free (blockids); */
/*     string_list_free (paths); */
/*     syncw_dirent_free (new_dent); */
/*     g_free (root_id); */
/*     g_free (canon_path); */

/*     if (ret == 0) */
/*         update_repo_size(repo_id); */

/*     return ret; */
/* } */

int
syncw_repo_manager_post_blocks (SyncwRepoManager *mgr,
                               const char *repo_id,
                               const char *blockids_json,
                               const char *paths_json,
                               const char *user,
                               GError **error)
{
    SyncwRepo *repo = NULL;
    GList *blockids = NULL, *paths = NULL, *ptr;
    int ret = 0;

    blockids = json_to_file_list (blockids_json);
    paths = json_to_file_list (paths_json);
    if (g_list_length(blockids) != g_list_length(paths)) {
        syncw_warning ("[post-blks] Invalid blockids or paths.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid files");
        ret = -1;
        goto out;
    }

    for (ptr = paths; ptr; ptr = ptr->next) {
        char *temp_file_path = ptr->data;
        if (g_access (temp_file_path, R_OK) != 0) {
            syncw_warning ("[post-blks] File block %s doesn't exist or not readable.\n",
                          temp_file_path);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                         "Invalid input file");
            ret = -1;
            goto out;
        }
    }

    GET_REPO_OR_FAIL(repo, repo_id);

    /* Write blocks. */
    if (syncw_fs_manager_index_raw_blocks (syncw->fs_mgr,
                                          repo->store_id,
                                          repo->version,
                                          paths,
                                          blockids) < 0) {
        syncw_warning ("Failed to index file blocks.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to index blocks");
        ret = -1;
        goto out;
    }

out:
    if (repo)
        syncw_repo_unref (repo);
    string_list_free (blockids);
    string_list_free (paths);

    if (ret == 0)
        update_repo_size(repo_id);

    return ret;
}

static int
check_quota_before_commit_blocks (const char *store_id,
                                  int version,
                                  GList *blockids)
{
    GList *ptr;
    char *blockid;
    gint64 total_size = 0;
    BlockMetadata *bmd;

    for (ptr = blockids; ptr; ptr = ptr->next) {
        blockid = ptr->data;
        bmd = syncw_block_manager_stat_block (syncw->block_mgr, store_id, version, blockid);
        if (!bmd) {
            syncw_warning ("Failed to stat block %s in store %s.\n",
                          blockid, store_id);
            return -1;
        }

        total_size += (gint64)bmd->size;
        g_free (bmd);
    }

    return syncw_quota_manager_check_quota_with_delta (syncw->quota_mgr, store_id, total_size);
}

int
syncw_repo_manager_commit_file_blocks (SyncwRepoManager *mgr,
                                      const char *repo_id,
                                      const char *parent_dir,
                                      const char *file_name,
                                      const char *blockids_json,
                                      const char *user,
                                      gint64 file_size,
                                      int replace_existed,
                                      char **new_id,
                                      GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char *canon_path = NULL;
    unsigned char sha1[20];
    char buf[SYNCW_PATH_MAX];
    char *root_id = NULL;
    SyncwDirent *new_dent = NULL;
    GList *blockids = NULL;
    char hex[41];
    int ret = 0;

    blockids = json_to_file_list (blockids_json);

    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    if (!canon_path)
        canon_path = get_canonical_path (parent_dir);

    if (should_ignore_file (file_name, NULL)) {
        syncw_warning ("[post-blks] Invalid filename %s.\n", file_name);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid filename");
        ret = -1;
        goto out;
    }

    if (strstr (parent_dir, "//") != NULL) {
        syncw_warning ("[post-blks] parent_dir cantains // sequence.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid parent dir");
        ret = -1;
        goto out;
    }

    int rc = check_quota_before_commit_blocks (repo->store_id, repo->version, blockids);
    if (rc != 0) {
        g_set_error (error, SYNCWERK_DOMAIN, POST_FILE_ERR_QUOTA_FULL,
                     "Quota full");
        ret = -1;
        goto out;
    }

    /* Write blocks. */
    if (syncw_fs_manager_index_existed_file_blocks (
            syncw->fs_mgr, repo->store_id, repo->version,
            blockids, sha1, file_size) < 0) {
        syncw_warning ("Failed to index existed file  blocks.\n");
        g_set_error (error, SYNCWERK_DOMAIN, POST_FILE_ERR_BLOCK_MISSING,
                     "Failed to index file blocks");
        ret = -1;
        goto out;
    }

    rawdata_to_hex(sha1, hex, 20);
    new_dent = syncw_dirent_new (dir_version_from_repo_version(repo->version),
                                hex, STD_FILE_MODE, file_name,
                                (gint64)time(NULL), user, file_size);

    root_id = do_post_file_replace (repo, head_commit->root_id,
                                    canon_path, replace_existed, new_dent);
    if (!root_id) {
        syncw_warning ("[post-blks] Failed to post file to %s in repo %s.\n",
                      canon_path, repo->id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to put file");
        ret = -1;
        goto out;
    }

    *new_id = g_strdup(hex);
    snprintf(buf, SYNCW_PATH_MAX, "Added \"%s\"", file_name);
    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, buf, NULL, error) < 0)
        ret = -1;

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref(head_commit);
    string_list_free (blockids);
    syncw_dirent_free (new_dent);
    g_free (root_id);
    g_free (canon_path);

    if (ret == 0)
        update_repo_size(repo_id);

    return ret;
}

static char *
del_file_recursive(SyncwRepo *repo,
                   const char *dir_id,
                   const char *to_path,
                   const char *filename)
{
    SyncwDir *olddir, *newdir;
    SyncwDirent *dent;
    GList *ptr;
    char *to_path_dup = NULL;
    char *remain = NULL;
    char *slash;
    char *id = NULL;
    char *ret = NULL;

    olddir = syncw_fs_manager_get_syncwdir_sorted(syncw->fs_mgr,
                                                repo->store_id, repo->version,
                                                dir_id);
    if (!olddir)
        return NULL;

    /* we reach the target dir. Remove the given entry from it. */
    if (*to_path == '\0') {
        SyncwDirent *old, *new;
        GList *newentries = NULL, *p;

        if (strchr(filename, '\t')) {
            char **file_names = g_strsplit (filename, "\t", -1); 
            int file_num = g_strv_length (file_names);
            int i, found_flag;

            for (p = olddir->entries; p != NULL; p = p->next) {
                found_flag = 0;
                old = p->data;
                for (i = 0; i < file_num; i++) {
                    if (strcmp(old->name, file_names[i]) == 0) {
                        found_flag = 1;
                        break;
                    }
                }
                if (!found_flag) {
                    new = syncw_dirent_dup (old);
                    newentries = g_list_prepend (newentries, new);
                }
            }
            g_strfreev (file_names);
        } else {
            for (p = olddir->entries; p != NULL; p = p->next) {
                old = p->data;
                if (strcmp(old->name, filename) != 0) {
                    new = syncw_dirent_dup (old);
                    newentries = g_list_prepend (newentries, new);
                }
            }
        }
        newentries = g_list_reverse (newentries);

        newdir = syncw_dir_new(NULL, newentries,
                              dir_version_from_repo_version(repo->version));
        if (syncw_dir_save(syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strdup(newdir->dir_id);
        syncw_dir_free(newdir);
        goto out;
    }

    to_path_dup = g_strdup (to_path);
    slash = strchr (to_path_dup, '/');

    if (!slash) {
        remain = to_path_dup + strlen(to_path_dup);
    } else {
        *slash = '\0';
        remain = slash + 1;
    }

    for (ptr = olddir->entries; ptr; ptr = ptr->next) {
        dent = (SyncwDirent *)ptr->data;

        if (strcmp(dent->name, to_path_dup) != 0)
            continue;

        id = del_file_recursive(repo, dent->id, remain, filename);
        if (id != NULL) {
            memcpy(dent->id, id, 40);
            dent->id[40] = '\0';
            if (repo->version > 0)
                dent->mtime = (guint64)time(NULL);
        }
        break;
    }
    if (id != NULL) {
        /* Create a new SyncwDir. */
        GList *new_entries;
        
        new_entries = dup_syncwdir_entries (olddir->entries);
        newdir = syncw_dir_new (NULL, new_entries,
                               dir_version_from_repo_version(repo->version));
        if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strdup (newdir->dir_id);
        syncw_dir_free (newdir);
    }

out:
    g_free (to_path_dup);
    g_free (id);
    syncw_dir_free(olddir);
    return ret;
}

static char *
do_del_file(SyncwRepo *repo,
            const char *root_id,
            const char *parent_dir,
            const char *file_name)
{
    /* if parent_dir is a absolutely path, we will remove the first '/' */
    if (*parent_dir == '/')
        parent_dir = parent_dir + 1;

    return del_file_recursive(repo, root_id, parent_dir, file_name);
}

int
syncw_repo_manager_del_file (SyncwRepoManager *mgr,
                            const char *repo_id,
                            const char *parent_dir,
                            const char *file_name,
                            const char *user,
                            GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char *canon_path = NULL;
    char **file_names;
    char buf[SYNCW_PATH_MAX];
    char *root_id = NULL;
    char *desc_file;
    int i = 0;
    int mode = 0;
    int ret = 0;
    int file_num = 0, empty_num = 0;

    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    if (!canon_path)
        canon_path = get_canonical_path (parent_dir);

    if (strchr(file_name, '\t')) {
        file_names = g_strsplit (file_name, "\t", -1);
        file_num = g_strv_length (file_names);

        for (i = 0; i < file_num; i++) {
            if (strcmp(file_names[i], "") == 0) {
                empty_num++;
                continue;
            }
            if (!check_file_exists(repo->store_id, repo->version,
                                   head_commit->root_id, canon_path, file_names[i], &mode)) {
                char *tmp_path;
                if (strcmp(canon_path, "") == 0)
                    tmp_path = "/";
                else
                    tmp_path = canon_path;
                syncw_warning ("[del file] File \'%s\' dosen't exist in %s in repo %s.\n",
                              file_names[i], tmp_path, repo->id);
                empty_num++;
                continue;
            }
            desc_file = file_names[i];
        }
        file_num -= empty_num;
        if (file_num <= 0)
            goto out;
    } else if (!check_file_exists(repo->store_id, repo->version,
                           head_commit->root_id, canon_path, file_name, &mode)) {
        goto out;
    }

    root_id = do_del_file (repo,
                           head_commit->root_id, canon_path, file_name);
    if (!root_id) {
        syncw_warning ("[del file] Failed to del file from %s in repo %s.\n",
                      canon_path, repo->id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to del file");
        ret = -1;
        goto out;
    }

    /* Commit. */
    if (file_num > 1) {
        snprintf(buf, SYNCW_PATH_MAX, "Deleted \"%s\" and %d more files",
                                      desc_file, file_num - 1);
    } else if (S_ISDIR(mode)) {
        snprintf(buf, SYNCW_PATH_MAX, "Removed directory \"%s\"", file_name);
    } else {
        snprintf(buf, SYNCW_PATH_MAX, "Deleted \"%s\"", file_name);
    }

    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, buf, NULL, error) < 0) {
        ret = -1;
        goto out;
    }

    syncw_repo_manager_cleanup_virtual_repos (mgr, repo_id);

    syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref(head_commit);
    if (file_num)
        g_strfreev (file_names);
    g_free (root_id);
    g_free (canon_path);

    if (ret == 0) {
        update_repo_size (repo_id);
    }

    return ret;
}

static SyncwDirent *
get_dirent_by_path (SyncwRepo *repo,
                    const char *root_id,
                    const char *path,
                    const char *file_name,
                    GError **error)
{
    SyncwCommit *head_commit = NULL; 
    SyncwDirent *dent = NULL;
    SyncwDir *dir = NULL;

    if (!root_id) {
        head_commit = syncw_commit_manager_get_commit(syncw->commit_mgr,
                                                     repo->id, repo->version, 
                                                     repo->head->commit_id);
        if (!head_commit) {
            syncw_warning ("commit %s:%s doesn't exist.\n",
                          repo->id, repo->head->commit_id);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid commit");
            goto out;
        }
        root_id = head_commit->root_id;
    }

    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               repo->store_id, repo->version,
                                               root_id,
                                               path, NULL);
    if (!dir) {
        syncw_warning ("dir %s doesn't exist in repo %s.\n", path, repo->id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid dir");
        goto out;
    }

    GList *p;
    for (p = dir->entries; p; p = p->next) {
        SyncwDirent *d = p->data;
        int r = strcmp (d->name, file_name);
        if (r == 0) {
            dent = syncw_dirent_dup(d);
            break;
        }
    }

    if (!dent && error && !(*error)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "failed to get dirent");
    }

out:
    if (head_commit)
        syncw_commit_unref (head_commit);
    if (dir)
        syncw_dir_free (dir);

    return dent;
}

static int
put_dirent_and_commit (SyncwRepo *repo,
                       const char *path,
                       SyncwDirent *dents[],
                       int n_dents,
                       int replace,
                       const char *user,
                       GError **error)
{
    SyncwCommit *head_commit = NULL;
    char *root_id = NULL;
    char buf[SYNCW_PATH_MAX];
    int ret = 0, i = 0;

    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);
    
    root_id = head_commit->root_id;

    GList *dent_list = NULL;
    GList *name_list = NULL;
    for (i = 0; i < n_dents; i++)
        dent_list = g_list_append (dent_list, dents[i]);

    if (*path == '/')
        path = path + 1;
    root_id = post_multi_files_recursive (repo, root_id, path, dent_list, user,
                                          replace, &name_list);
    g_list_free (dent_list);
    g_list_free_full (name_list, (GDestroyNotify)g_free);

    if (!root_id) {
        if (n_dents > 1)
            syncw_warning ("[cp file] Failed to cp %s and other %d files to %s in repo %s.\n",
                          dents[0]->name, n_dents - 1, path, repo->id);
        else
            syncw_warning ("[cp file] Failed to cp %s to %s in repo %s.\n",
                          dents[0]->name, path, repo->id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                "Failed to cp file");
        ret = -1;
        goto out;
    }

    /* Commit. */
    if (n_dents > 1) {
        snprintf(buf, sizeof(buf), "Added \"%s\" and %d more files",
                                   dents[0]->name, n_dents - 1);
    } else if (S_ISDIR(dents[0]->mode)) {
        snprintf(buf, sizeof(buf), "Added directory \"%s\"", dents[0]->name);
    } else {
        snprintf(buf, sizeof(buf), "Added \"%s\"", dents[0]->name);
    }

    if (gen_new_commit (repo->id, head_commit, root_id,
                        user, buf, NULL, error) < 0)
        ret = -1;

out:
    if (head_commit)
        syncw_commit_unref (head_commit);
    if (root_id)
        g_free (root_id);
    
    return ret;
}

static char *
copy_syncwerk (SyncwRepo *src_repo, SyncwRepo *dst_repo, const char *file_id,
              CopyTask *task, guint64 *size)
{
    Syncwerk *file;

    file = syncw_fs_manager_get_syncwerk (syncw->fs_mgr,
                                        src_repo->store_id, src_repo->version,
                                        file_id);
    if (!file) {
        syncw_warning ("Failed to get file object %s from repo %s.\n",
                      file_id, src_repo->id);
        return NULL;
    }

    /* We may be copying from v0 repo to v1 repo or vise versa. */
    file->version = syncwerk_version_from_repo_version(dst_repo->version);

    if (syncwerk_save (syncw->fs_mgr,
                      dst_repo->store_id,
                      dst_repo->version,
                      file) < 0) {
        syncw_warning ("Failed to copy file object %s from repo %s to %s.\n",
                      file_id, src_repo->id, dst_repo->id);
        syncwerk_unref (file);
        return NULL;
    }

    int i;
    char *block_id;
    for (i = 0; i < file->n_blocks; ++i) {
        /* Check cancel before copying a block. */
        if (task && g_atomic_int_get (&task->canceled)) {
            syncwerk_unref (file);
            return NULL;
        }

        block_id = file->blk_sha1s[i];
        if (syncw_block_manager_copy_block (syncw->block_mgr,
                                           src_repo->store_id, src_repo->version,
                                           dst_repo->store_id, dst_repo->version,
                                           block_id) < 0) {
            syncw_warning ("Failed to copy block %s from repo %s to %s.\n",
                          block_id, src_repo->id, dst_repo->id);
            syncwerk_unref (file);
            return NULL;
        }
    }

    if (task)
        ++(task->done);

    *size = file->file_size;
    char *ret = g_strdup(file->file_id);

    syncwerk_unref (file);
    return ret;
}

static char *
copy_recursive (SyncwRepo *src_repo, SyncwRepo *dst_repo,
                const char *obj_id, guint32 mode, const char *modifier,
                CopyTask *task, guint64 *size)
{
    if (S_ISREG(mode)) {
        return copy_syncwerk (src_repo, dst_repo, obj_id, task, size);
    } else if (S_ISDIR(mode)) {
        SyncwDir *src_dir = NULL, *dst_dir = NULL;
        GList *dst_ents = NULL, *ptr;
        char *new_id = NULL;
        SyncwDirent *dent, *new_dent = NULL;

        src_dir = syncw_fs_manager_get_syncwdir (syncw->fs_mgr,
                                               src_repo->store_id,
                                               src_repo->version,
                                               obj_id);
        if (!src_dir) {
            syncw_warning ("Syncwdir %s doesn't exist in repo %s.\n",
                          obj_id, src_repo->id);
            return NULL;
        }

        for (ptr = src_dir->entries; ptr; ptr = ptr->next) {
            dent = ptr->data;

            guint64 new_size = 0;
            new_id = copy_recursive (src_repo, dst_repo,
                                     dent->id, dent->mode, modifier, task, &new_size);
            if (!new_id) {
                syncw_dir_free (src_dir);
                return NULL;
            }

            new_dent = syncw_dirent_new (dir_version_from_repo_version(dst_repo->version),
                                        new_id, dent->mode, dent->name,
                                        dent->mtime, modifier, new_size);
            dst_ents = g_list_prepend (dst_ents, new_dent);
            g_free (new_id);
        }
        dst_ents = g_list_reverse (dst_ents);

        syncw_dir_free (src_dir);

        dst_dir = syncw_dir_new (NULL, dst_ents,
                                dir_version_from_repo_version(dst_repo->version));
        if (syncw_dir_save (syncw->fs_mgr,
                           dst_repo->store_id, dst_repo->version,
                           dst_dir) < 0) {
            syncw_warning ("Failed to save new dir.\n");
            syncw_dir_free (dst_dir);
            return NULL;
        }

        char *ret = g_strdup(dst_dir->dir_id);
        *size = 0;
        syncw_dir_free (dst_dir);
        return ret;
    }

    return NULL;
}

static int
cross_repo_copy (const char *src_repo_id,
                 const char *src_path,
                 const char *src_filename,
                 const char *dst_repo_id,
                 const char *dst_path,
                 const char *dst_filename,
                 int replace,
                 const char *modifier,
                 CopyTask *task)
{
    SyncwRepo *src_repo = NULL, *dst_repo = NULL;
    SyncwDirent *src_dent = NULL, *dst_dent = NULL;
    SyncwDirent **src_dents = NULL, **dst_dents = NULL;
    char **src_names = NULL, **dst_names = NULL;
    char *new_id = NULL;
    guint64 new_size = 0;
    int ret = 0, i = 0;
    int file_num = 1;

    src_repo = syncw_repo_manager_get_repo (syncw->repo_mgr, src_repo_id);
    if (!src_repo) {
        ret = -1;
        goto out;
    }

    dst_repo = syncw_repo_manager_get_repo (syncw->repo_mgr, dst_repo_id);
    if (!dst_repo) {
        ret = -1;
        goto out;
    }

    /* get src dirents */
    if (strchr(src_filename, '\t') && strchr(dst_filename, '\t')) {
        src_names = g_strsplit (src_filename, "\t", -1); 
        dst_names = g_strsplit (dst_filename, "\t", -1); 
        file_num = g_strv_length (src_names);

        src_dents = g_new0 (SyncwDirent *, file_num);
        dst_dents = g_new0 (SyncwDirent *, file_num);

        for (i = 0; i < file_num; i++) {
            if (strcmp(src_names[i], "") == 0) { 
                ret = -1;
                syncw_warning ("[copy files] Bad args: Empty src_filename.\n");
                goto out; 
            }    
            src_dents[i] = get_dirent_by_path (src_repo, NULL,
                                               src_path, src_names[i], NULL);
            if (!src_dents[i]) {
                ret = -1;
                syncw_warning ("[copy files] File %s not Found.\n", src_names[i]);
                goto out; 
            }    
        }

        for (i = 0; i < file_num; i++) {
            new_id = copy_recursive (src_repo, dst_repo,
                                     src_dents[i]->id, src_dents[i]->mode, modifier, task,
                                     &new_size);
            if (!new_id) {
                ret = -1;
                syncw_warning ("[copy files] Failed to copy file %s.\n", src_dents[i]->name);
                goto out;
            }
            dst_dents[i] = syncw_dirent_new (dir_version_from_repo_version(dst_repo->version),
                                            new_id, src_dents[i]->mode, dst_names[i],
                                            src_dents[i]->mtime, modifier, new_size);
            g_free (new_id);
        }
        
    } else {
        src_dent = get_dirent_by_path (src_repo, NULL,
                                       src_path, src_filename, NULL);
        if (!src_dent) {
            ret = -1;
            goto out;
        }
        new_id = copy_recursive (src_repo, dst_repo,
                                 src_dent->id, src_dent->mode, modifier, task,
                                 &new_size);
        if (!new_id) {
            ret = -1;
            goto out;
        }

        dst_dent = syncw_dirent_new (dir_version_from_repo_version(dst_repo->version),
                                    new_id, src_dent->mode, dst_filename,
                                    src_dent->mtime, modifier, new_size);
        g_free (new_id);

    }
    if (put_dirent_and_commit (dst_repo,
                               dst_path,
                               file_num > 1 ? dst_dents : &dst_dent,
                               file_num,
                               replace,
                               modifier,
                               NULL) < 0) {
        ret = -1;
        goto out;
    }

    if (task)
        task->successful = TRUE;

    syncw_repo_manager_merge_virtual_repo (syncw->repo_mgr, dst_repo_id, NULL);

out:
    if (src_repo)
        syncw_repo_unref (src_repo);
    if (dst_repo)
        syncw_repo_unref (dst_repo);
    if (src_dent)
        syncw_dirent_free(src_dent);
    if (dst_dent)
        syncw_dirent_free(dst_dent);
    if (file_num > 1) { 
        for (i = 0; i < file_num; i++) {
            syncw_dirent_free(src_dents[i]);
            syncw_dirent_free(dst_dents[i]);
        }    
        g_free (src_dents);
        g_free (dst_dents);
        g_strfreev(src_names);
        g_strfreev(dst_names);
    }

    if (ret == 0) {
        update_repo_size (dst_repo_id);
    } else {
        if (task && !task->canceled)
            task->failed = TRUE;
    }

    return ret;
}

static gboolean
is_virtual_repo_and_origin (SyncwRepo *repo1, SyncwRepo *repo2)
{
    if (repo1->virtual_info &&
        strcmp (repo1->virtual_info->origin_repo_id, repo2->id) == 0)
        return TRUE;
    if (repo2->virtual_info &&
        strcmp (repo2->virtual_info->origin_repo_id, repo1->id) == 0)
        return TRUE;
    return FALSE;
}

static gboolean
check_file_count_and_size (SyncwRepo *repo, SyncwDirent *dent, gint64 total_files,
                           gint64 *multi_file_size, GError **error)
{
    if (syncw->copy_mgr->max_files > 0 &&
        total_files > syncw->copy_mgr->max_files) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Too many files");
        return FALSE;
    }

    if (syncw->copy_mgr->max_size > 0) {
        gint64 size = -1;

        if (S_ISREG(dent->mode)) {
            if (repo->version > 0)
                size = dent->size;
            else
                size = syncw_fs_manager_get_file_size (syncw->fs_mgr,
                                                      repo->store_id,
                                                      repo->version,
                                                      dent->id);
        } else {
            size = syncw_fs_manager_get_fs_size (syncw->fs_mgr,
                                                repo->store_id,
                                                repo->version,
                                                dent->id);
        }
        if (size < 0) {
            syncw_warning ("Failed to get dir size of %s:%s.\n",
                          repo->store_id, dent->id);
            return FALSE;
        }
        if (multi_file_size) {
            *multi_file_size += size;
            if (*multi_file_size > syncw->copy_mgr->max_size) {
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                             "Folder or file size is too large");
                return FALSE;
            }
        }

        if (size > syncw->copy_mgr->max_size) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "Folder or file size is too large");
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * Copy a SyncwDirent from a SyncwDir to another.
 * 
 * 1. When @src_repo and @dst_repo are not the same repo, neither of them
 *    should be encrypted.
 * 
 * 2. the file being copied must not exist in the dst path of the dst repo.
 */
SyncwerkCopyResult *
syncw_repo_manager_copy_file (SyncwRepoManager *mgr,
                             const char *src_repo_id,
                             const char *src_path,
                             const char *src_filename,
                             const char *dst_repo_id,
                             const char *dst_path,
                             const char *dst_filename,
                             const char *user,
                             int need_progress,
                             int synchronous,
                             GError **error)
{
    SyncwRepo *src_repo = NULL, *dst_repo = NULL;
    SyncwDirent *src_dent = NULL, *dst_dent = NULL;
    char *src_canon_path = NULL, *dst_canon_path = NULL;
    SyncwCommit *dst_head_commit = NULL;
    int ret = 0;
    gboolean background = FALSE;
    char *task_id = NULL;
    SyncwerkCopyResult *res= NULL;

    GET_REPO_OR_FAIL(src_repo, src_repo_id);

    if (strcmp(src_repo_id, dst_repo_id) != 0) {
        GET_REPO_OR_FAIL(dst_repo, dst_repo_id);

        if (src_repo->encrypted || dst_repo->encrypted) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                         "Can't copy files between encrypted repo(s)");
            ret = -1;
            goto out;
        }
        
    } else {
        syncw_repo_ref (src_repo);
        dst_repo = src_repo;
    }
    
    src_canon_path = get_canonical_path (src_path);
    dst_canon_path = get_canonical_path (dst_path);

    GET_COMMIT_OR_FAIL(dst_head_commit,
                       dst_repo->id, dst_repo->version, 
                       dst_repo->head->commit_id);
    
    /* FAIL_IF_FILE_EXISTS(dst_repo->store_id, dst_repo->version,
                        dst_head_commit->root_id, dst_canon_path, dst_filename, NULL); */

    /* get src dirent */
    src_dent = get_dirent_by_path (src_repo, NULL,
                                   src_canon_path, src_filename, error);
    if (!src_dent) {
        ret = -1;
        goto out;
    }

    if (strcmp (src_repo_id, dst_repo_id) == 0 ||
        is_virtual_repo_and_origin (src_repo, dst_repo)) {

        gint64 file_size = (src_dent->version > 0) ? src_dent->size : -1;

        /* duplicate src dirent with new name */
        dst_dent = syncw_dirent_new (dir_version_from_repo_version(dst_repo->version),
                                    src_dent->id, src_dent->mode, dst_filename,
                                    src_dent->mtime, user, file_size);

        if (put_dirent_and_commit (dst_repo,
                                   dst_canon_path,
                                   &dst_dent,
                                   1,
                                   0,
                                   user,
                                   error) < 0) {
            if (!error)
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                             "failed to put dirent");
            ret = -1;
            goto out;
        }

        syncw_repo_manager_merge_virtual_repo (mgr, dst_repo_id, NULL);

        update_repo_size (dst_repo_id);
    } else if (!synchronous) {
        background = TRUE;

        gint64 total_files = -1;
        if (S_ISDIR(src_dent->mode))
            total_files = syncw_fs_manager_count_fs_files (syncw->fs_mgr,
                                                          src_repo->store_id,
                                                          src_repo->version,
                                                          src_dent->id);
        else
            total_files = 1;
        if (total_files < 0) {
            syncw_warning ("Failed to get file count.\n");
            ret = -1;
            goto out;
        }

        if (!check_file_count_and_size (src_repo, src_dent, total_files, NULL, error)) {
            ret = -1;
            goto out;
        }

        task_id = syncw_copy_manager_add_task (syncw->copy_mgr,
                                              src_repo_id,
                                              src_canon_path,
                                              src_filename,
                                              dst_repo_id,
                                              dst_canon_path,
                                              dst_filename,
                                              0,
                                              user,
                                              total_files,
                                              cross_repo_copy,
                                              need_progress);
        if (need_progress && !task_id) {
            syncw_warning ("Failed to start copy task.\n");
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "failed to start copy task");
            ret = -1;
            goto out;
        }
    } else {
        /* Synchronous for cross-repo copy */
        if (cross_repo_copy (src_repo_id,
                             src_canon_path,
                             src_filename,
                             dst_repo_id,
                             dst_canon_path,
                             dst_filename,
                             0,
                             user,
                             NULL) < 0) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "Failed to move");
            ret = -1;
            goto out;
        }
    }

out:
    if (src_repo)
        syncw_repo_unref (src_repo);
    if (dst_repo)
        syncw_repo_unref (dst_repo);
    if (dst_head_commit)
        syncw_commit_unref(dst_head_commit);
    if (src_canon_path)
        g_free (src_canon_path);
    if (dst_canon_path)
        g_free (dst_canon_path);
    if (src_dent)
        syncw_dirent_free(src_dent);
    if (dst_dent)
        syncw_dirent_free(dst_dent);

    if (ret == 0) {
        res = syncwerk_copy_result_new ();
        g_object_set (res, "background", background, "task_id", task_id, NULL);
        g_free (task_id);
    }

    return res;
}

SyncwerkCopyResult *
syncw_repo_manager_copy_multiple_files (SyncwRepoManager *mgr,
                                       const char *src_repo_id,
                                       const char *src_path,
                                       const char *src_filenames,
                                       const char *dst_repo_id,
                                       const char *dst_path,
                                       const char *dst_filenames,
                                       const char *user,
                                       int need_progress,
                                       int synchronous,
                                       GError **error)
{
    SyncwRepo *src_repo = NULL, *dst_repo = NULL;
    SyncwDirent **src_dents = NULL, **dst_dents = NULL;
    char *src_canon_path = NULL, *dst_canon_path = NULL;
    SyncwCommit *dst_head_commit = NULL;
    int i = 0, ret = 0; 
    int file_num = 1; 
    gint64 *file_sizes = NULL;
    gboolean background = FALSE;
    char *task_id = NULL;
    char **src_names = NULL, **dst_names = NULL;
    SyncwerkCopyResult *res = NULL;

    GET_REPO_OR_FAIL(src_repo, src_repo_id);
    
    if (strcmp(src_repo_id, dst_repo_id) != 0) { 
        GET_REPO_OR_FAIL(dst_repo, dst_repo_id);

        if (src_repo->encrypted || dst_repo->encrypted) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                         "Can't copy files between encrypted repo(s)");
            ret = -1;
            goto out;
        }

    } else {
        syncw_repo_ref (src_repo);
        dst_repo = src_repo;
    }

    src_canon_path = get_canonical_path (src_path);
    dst_canon_path = get_canonical_path (dst_path);

    GET_COMMIT_OR_FAIL(dst_head_commit,
                       dst_repo->id, dst_repo->version,
                       dst_repo->head->commit_id);
    /*FAIL_IF_FILE_EXISTS(dst_repo->store_id, dst_repo->version,
                        dst_head_commit->root_id, dst_canon_path, dst_filename, NULL);*/

    /* get src dirents */
    if (strchr(src_filenames, '\t') && strchr(dst_filenames, '\t')) {
        src_names = g_strsplit (src_filenames, "\t", -1);
        dst_names = g_strsplit (dst_filenames, "\t", -1);
        file_num = g_strv_length (src_names);
        int dst_file_num = g_strv_length (dst_names);
        if (dst_file_num != file_num) {
            ret = -1;
            syncw_warning ("[copy files] Bad args.\n");
            goto out; 
        }

        src_dents = g_new0 (SyncwDirent *, file_num);
        file_sizes = g_new0 (gint64, file_num);

        for (i = 0; i < file_num; i++) {
            if (strcmp(src_names[i], "") == 0) {
                ret = -1;
                syncw_warning ("[copy files] Bad args: Empty src_filenames.\n");
                goto out;
            }
            src_dents[i] = get_dirent_by_path (src_repo, NULL,
                                               src_canon_path, src_names[i], error);
            if (!src_dents[i]) {
                ret = -1;
                syncw_warning ("[copy files] File %s not Found.\n", src_names[i]);
                goto out;
            }
            file_sizes[i] = (src_dents[i]->version > 0) ? src_dents[i]->size : -1;
        }
    } else {
        ret = -1;
        syncw_warning ("[copy files] Bad args: Split filenames with '\\t'.\n");
        goto out;
    }
    
    /* copy file within the same repo */
    if (src_repo == dst_repo || 
        is_virtual_repo_and_origin (src_repo, dst_repo)) {
        dst_dents = g_new0 (SyncwDirent *, file_num);
        for (i = 0; i < file_num; i++) {
            if (strcmp(dst_names[i], "") == 0) {
                ret = -1;
                syncw_warning ("[copy files] Bad args: Empty dst_filenames.\n");
                goto out;
            }
            /* duplicate src dirents with new names */
            dst_dents[i] = syncw_dirent_new (dir_version_from_repo_version (dst_repo->version),
                                            src_dents[i]->id, src_dents[i]->mode, dst_names[i],
                                            src_dents[i]->mtime, user, file_sizes[i]);
        }
        if (put_dirent_and_commit (dst_repo,
                                   dst_canon_path,
                                   dst_dents,
                                   file_num,
                                   0,
                                   user,
                                   error) < 0) {
            if (!error)
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                             "failed to put dirents");
            ret = -1;
            goto out;
        }

        syncw_repo_manager_merge_virtual_repo (mgr, src_repo_id, NULL);

        update_repo_size (dst_repo_id);
    } else {
        /* copy between different repos */
        if (!synchronous) {
            background = TRUE;

            gint64 total_files = -1;
            gint64 total_files_all = 0;
            gint64 total_size_all = 0;

            for (i = 0; i < file_num; i++) {
                if (S_ISDIR(src_dents[i]->mode))
                    total_files = syncw_fs_manager_count_fs_files (syncw->fs_mgr,
                                                                  src_repo->store_id,
                                                                  src_repo->version,
                                                                  src_dents[i]->id);
                else
                    total_files = 1;
                if (total_files < 0) {
                    syncw_warning ("Failed to get file count.\n");
                    ret = -1;
                    goto out;
                }
                total_files_all += total_files;
                if (!check_file_count_and_size (src_repo, src_dents[i], total_files_all,
                                                &total_size_all, error)) {
                    ret = -1;
                    goto out;
                }
            }
            task_id = syncw_copy_manager_add_task (syncw->copy_mgr,
                                                  src_repo_id,
                                                  src_canon_path,
                                                  src_filenames,
                                                  dst_repo_id,
                                                  dst_canon_path,
                                                  dst_filenames,
                                                  0,
                                                  user,
                                                  total_files_all,
                                                  cross_repo_copy,
                                                  need_progress);
            if (need_progress && !task_id) {
                syncw_warning ("Failed to start copy task.\n");
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                        "failed to start copy task");
                ret = -1;
                goto out; 
            }
        } else {
            /* Synchronous for cross-repo copy */
            if (cross_repo_copy (src_repo_id,
                                 src_canon_path,
                                 src_filenames,
                                 dst_repo_id,
                                 dst_canon_path,
                                 dst_filenames,
                                 0,
                                 user,
                                 NULL) < 0) { 
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                             "Failed to move");
                ret = -1;
                goto out; 
            }
        } // Synchronous copy
    } //else diffrent repo

out:
    if (src_repo) syncw_repo_unref (src_repo);
    if (dst_repo) syncw_repo_unref (dst_repo);

    if (dst_head_commit) syncw_commit_unref(dst_head_commit);
    
    if (src_canon_path) g_free (src_canon_path);
    if (dst_canon_path) g_free (dst_canon_path);

    if (src_names)
        g_strfreev (src_names);
    if (dst_names)
        g_strfreev (dst_names);
    if (file_sizes)
        g_free (file_sizes);
    if (src_dents) {
        for (i = 0; i < file_num; i++)
            syncw_dirent_free (src_dents[i]);
        g_free (src_dents);
    }
    if (dst_dents) {
        for (i = 0; i < file_num; i++)
            syncw_dirent_free (dst_dents[i]);
        g_free (dst_dents);
    }
    if (ret == 0) { 
        res = syncwerk_copy_result_new ();
        g_object_set (res, "background", background, "task_id", task_id, NULL);
        g_free (task_id);
    }    

    return res;
}

static int
move_file_same_repo (const char *repo_id,
                     const char *src_path, SyncwDirent *src_dents[],
                     const char *dst_path, SyncwDirent *dst_dents[],
                     int file_num,
                     int replace,
                     const char *user,
                     GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char *root_id_after_put = NULL, *root_id = NULL;
    char buf[SYNCW_PATH_MAX];
    int ret = 0, i = 0;
    GString *filenames_str = NULL;

    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    filenames_str = g_string_new ("");
    root_id_after_put = head_commit->root_id;

    GList *dent_list = NULL;
    GList *name_list = NULL;
    for (i = 0; i < file_num; i++) {
        dent_list = g_list_append (dent_list, dst_dents[i]);
        g_string_append_printf (filenames_str, "%s", src_dents[i]->name);
        if ((i + 1) < file_num)
            g_string_append_printf (filenames_str, "\t");
    }    
    if (*dst_path == '/') 
        dst_path = dst_path + 1; 

    root_id_after_put = post_multi_files_recursive (repo, head_commit->root_id, dst_path, dent_list, user,
                                                    replace, &name_list);
    g_list_free (dent_list);
    g_list_free_full (name_list, (GDestroyNotify)g_free);

    if (!root_id_after_put) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "move file failed");
        ret = -1;
        goto out;
    }
    root_id = do_del_file (repo, root_id_after_put, src_path, filenames_str->str);

    if (!root_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "move file failed");
        ret = -1;
        goto out;
    }

    /* Commit. */
    if (file_num > 1) {
        snprintf(buf, SYNCW_PATH_MAX, "Moved \"%s\" and %d more files",
                                      src_dents[0]->name,file_num - 1);
    } else if (S_ISDIR(src_dents[0]->mode)) {
        snprintf(buf, SYNCW_PATH_MAX, "Moved directory \"%s\"", src_dents[0]->name);
    } else {
        snprintf(buf, SYNCW_PATH_MAX, "Moved \"%s\"", src_dents[0]->name);
    }

    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, buf, NULL, error) < 0)
        ret = -1;
    
out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref (head_commit);
    if (filenames_str)
        g_string_free (filenames_str, TRUE);
        
    g_free (root_id_after_put);
    g_free (root_id);
    
    return ret;
}

static int
cross_repo_move (const char *src_repo_id,
                 const char *src_path,
                 const char *src_filename,
                 const char *dst_repo_id,
                 const char *dst_path,
                 const char *dst_filename,
                 int replace,
                 const char *modifier,
                 CopyTask *task)
{
    SyncwRepo *src_repo = NULL, *dst_repo = NULL;
    SyncwDirent *src_dent = NULL, *dst_dent = NULL;
    SyncwDirent **src_dents = NULL, **dst_dents = NULL;
    char **src_names = NULL, **dst_names = NULL;
    char *new_id = NULL;
    guint64 new_size = 0;
    int ret = 0, i = 0;
    int file_num = 1;

    src_repo = syncw_repo_manager_get_repo (syncw->repo_mgr, src_repo_id);
    if (!src_repo) {
        ret = -1;
        goto out;
    }

    dst_repo = syncw_repo_manager_get_repo (syncw->repo_mgr, dst_repo_id);
    if (!dst_repo) {
        ret = -1;
        goto out;
    }

    /* get src dirents */
    if (strchr(src_filename, '\t') && strchr(dst_filename, '\t')) {
        src_names = g_strsplit (src_filename, "\t", -1); 
        dst_names = g_strsplit (dst_filename, "\t", -1); 
        file_num = g_strv_length (src_names);

        src_dents = g_new0 (SyncwDirent *, file_num);
        dst_dents = g_new0 (SyncwDirent *, file_num);

        for (i = 0; i < file_num; i++) {
            if (strcmp(src_names[i], "") == 0) { 
                ret = -1;
                syncw_warning ("[move files] Bad args: Empty src_filename.\n");
                goto out; 
            }    
            src_dents[i] = get_dirent_by_path (src_repo, NULL,
                                               src_path, src_names[i], NULL);
            if (!src_dents[i]) {
                ret = -1;
                syncw_warning ("[move files] File %s not Found.\n", src_names[i]);
                goto out; 
            }    
        }

        for (i = 0; i < file_num; i++) {
            new_id = copy_recursive (src_repo, dst_repo,
                                     src_dents[i]->id, src_dents[i]->mode, modifier, task,
                                     &new_size);
            if (!new_id) {
                ret = -1;
                syncw_warning ("[move files] Failed to copy file %s.\n", src_dents[i]->name);
                goto out; 
            }    
            dst_dents[i] = syncw_dirent_new (dir_version_from_repo_version(dst_repo->version),
                                            new_id, src_dents[i]->mode, dst_names[i],
                                            src_dents[i]->mtime, modifier, new_size);
            g_free (new_id);
        }    
    } else { 
        src_dent = get_dirent_by_path (src_repo, NULL,
                                       src_path, src_filename, NULL);
        if (!src_dent) {
            ret = -1;
            goto out;
        }
        new_id = copy_recursive (src_repo, dst_repo,
                                 src_dent->id, src_dent->mode, modifier, task,
                                 &new_size);
        if (!new_id) {
            ret = -1;
            goto out;
        }

        dst_dent = syncw_dirent_new (dir_version_from_repo_version(dst_repo->version),
                                    new_id, src_dent->mode, dst_filename,
                                    src_dent->mtime, modifier, new_size);
        g_free (new_id);

    }

    if (put_dirent_and_commit (dst_repo,
                               dst_path,
                               file_num > 1 ? dst_dents : &dst_dent,
                               file_num,
                               replace,
                               modifier,
                               NULL) < 0) {
        ret = -1;
        goto out;
    }

    syncw_repo_manager_merge_virtual_repo (syncw->repo_mgr, dst_repo_id, NULL);

    if (syncw_repo_manager_del_file (syncw->repo_mgr, src_repo_id, src_path,
                                    src_filename, modifier, NULL) < 0) {
        ret = -1;
        goto out;
    }

    if (task)
        task->successful = TRUE;

    syncw_repo_manager_merge_virtual_repo (syncw->repo_mgr, src_repo_id, NULL);

out:
    if (src_repo)
        syncw_repo_unref (src_repo);
    if (dst_repo)
        syncw_repo_unref (dst_repo);
    if (src_dent)
        syncw_dirent_free(src_dent);
    if (dst_dent)
        syncw_dirent_free(dst_dent);
    if (file_num > 1) {
        for (i = 0; i < file_num; i++) {
            syncw_dirent_free(src_dents[i]);
            syncw_dirent_free(dst_dents[i]);
        }
        g_free (src_dents);
        g_free (dst_dents);
        g_strfreev(src_names);
        g_strfreev(dst_names);
    }

    if (ret == 0) {
        update_repo_size (dst_repo_id);
    } else {
        if (task && !task->canceled)
            task->failed = TRUE;
    }

    return ret;
}
                     
SyncwerkCopyResult *
syncw_repo_manager_move_file (SyncwRepoManager *mgr,
                             const char *src_repo_id,
                             const char *src_path,
                             const char *src_filename,
                             const char *dst_repo_id,
                             const char *dst_path,
                             const char *dst_filename,
                             int replace,
                             const char *user,
                             int need_progress,
                             int synchronous,
                             GError **error)
{
    SyncwRepo *src_repo = NULL, *dst_repo = NULL;
    SyncwDirent *src_dent = NULL, *dst_dent = NULL;
    char *src_canon_path = NULL, *dst_canon_path = NULL;
    SyncwCommit *dst_head_commit = NULL;
    int ret = 0;
    gboolean background = FALSE;
    char *task_id = NULL;
    SyncwerkCopyResult *res = NULL;

    GET_REPO_OR_FAIL(src_repo, src_repo_id);

    if (strcmp(src_repo_id, dst_repo_id) != 0) {
        GET_REPO_OR_FAIL(dst_repo, dst_repo_id);

        if (src_repo->encrypted || dst_repo->encrypted) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                         "Can't copy files between encrypted repo(s)");
            ret = -1;
            goto out;
        }
        
    } else {
        syncw_repo_ref (src_repo);
        dst_repo = src_repo;
    }
    
    src_canon_path = get_canonical_path (src_path);
    dst_canon_path = get_canonical_path (dst_path);
    /* first check whether a file with file_name already exists in destination dir */
    GET_COMMIT_OR_FAIL(dst_head_commit,
                       dst_repo->id, dst_repo->version, 
                       dst_repo->head->commit_id);
    /*FAIL_IF_FILE_EXISTS(dst_repo->store_id, dst_repo->version,
                        dst_head_commit->root_id, dst_canon_path, dst_filename, NULL);*/

    /* get src dirent */
    src_dent = get_dirent_by_path (src_repo, NULL,
                                   src_canon_path, src_filename, error);
    if (!src_dent) {
        ret = -1;
        goto out;
    }
    gint64 file_size = (src_dent->version > 0) ? src_dent->size : -1;

    if (src_repo == dst_repo) {
        /* duplicate src dirent with new name */
        dst_dent = syncw_dirent_new (dir_version_from_repo_version (dst_repo->version),
                                    src_dent->id, src_dent->mode, dst_filename,
                                    src_dent->mtime, user, file_size);

        /* move file within the same repo */
        if (move_file_same_repo (src_repo_id,
                                 src_canon_path, &src_dent,
                                 dst_canon_path, &dst_dent,
                                 1, replace, user, error) < 0) {
            ret = -1;
            goto out;
        }

        syncw_repo_manager_cleanup_virtual_repos (mgr, src_repo_id);
        syncw_repo_manager_merge_virtual_repo (mgr, src_repo_id, NULL);

        update_repo_size (dst_repo_id);
    } else {
        /* move between different repos */

        if (is_virtual_repo_and_origin (src_repo, dst_repo)) {
            /* duplicate src dirent with new name */
            dst_dent = syncw_dirent_new (dir_version_from_repo_version(dst_repo->version),
                                        src_dent->id, src_dent->mode, dst_filename,
                                        src_dent->mtime, user, file_size);

            /* add this dirent to dst repo */
            if (put_dirent_and_commit (dst_repo,
                                       dst_canon_path,
                                       &dst_dent,
                                       1,
                                       replace,
                                       user,
                                       error) < 0) {
                if (!error)
                    g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                                 "failed to put dirent");
                ret = -1;
                goto out;
            }

            syncw_repo_manager_merge_virtual_repo (mgr, dst_repo_id, NULL);

            if (syncw_repo_manager_del_file (mgr, src_repo_id, src_path,
                                            src_filename, user, error) < 0) {
                ret = -1;
                goto out;
            }

            syncw_repo_manager_merge_virtual_repo (mgr, src_repo_id, NULL);

            update_repo_size (dst_repo_id);
        } else if (!synchronous) {
            background = TRUE;

            gint64 total_files = -1;
            if (S_ISDIR(src_dent->mode))
                total_files = syncw_fs_manager_count_fs_files (syncw->fs_mgr,
                                                              src_repo->store_id,
                                                              src_repo->version,
                                                              src_dent->id);
            else
                total_files = 1;
            if (total_files < 0) {
                syncw_warning ("Failed to get file count.\n");
                ret = -1;
                goto out;
            }

            if (!check_file_count_and_size (src_repo, src_dent, total_files, NULL, error)) {
                ret = -1;
                goto out;
            }

            task_id = syncw_copy_manager_add_task (syncw->copy_mgr,
                                                  src_repo_id,
                                                  src_canon_path,
                                                  src_filename,
                                                  dst_repo_id,
                                                  dst_canon_path,
                                                  dst_filename,
                                                  replace,
                                                  user,
                                                  total_files,
                                                  cross_repo_move,
                                                  need_progress);
            if (need_progress && !task_id) {
                syncw_warning ("Failed to start copy task.\n");
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                             "failed to start copy task");
                ret = -1;
                goto out;
            }
        } else {
            /* Synchronous for cross-repo move */
            if (cross_repo_move (src_repo_id,
                                 src_canon_path,
                                 src_filename,
                                 dst_repo_id,
                                 dst_canon_path,
                                 dst_filename,
                                 replace,
                                 user,
                                 NULL) < 0) {
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                             "Failed to move");
                ret = -1;
                goto out;
            }
        }
    }

out:
    if (src_repo) syncw_repo_unref (src_repo);
    if (dst_repo) syncw_repo_unref (dst_repo);

    if (dst_head_commit) syncw_commit_unref(dst_head_commit);
    
    if (src_canon_path) g_free (src_canon_path);
    if (dst_canon_path) g_free (dst_canon_path);
    
    syncw_dirent_free(src_dent);
    syncw_dirent_free(dst_dent);

    if (ret == 0) {
        res = syncwerk_copy_result_new ();
        g_object_set (res, "background", background, "task_id", task_id, NULL);
        g_free (task_id);
    }

    return res;
}

SyncwerkCopyResult *
syncw_repo_manager_move_multiple_files (SyncwRepoManager *mgr,
                                       const char *src_repo_id,
                                       const char *src_path,
                                       const char *src_filenames,
                                       const char *dst_repo_id,
                                       const char *dst_path,
                                       const char *dst_filenames,
                                       int replace,
                                       const char *user,
                                       int need_progress,
                                       int synchronous,
                                       GError **error)
{
    SyncwRepo *src_repo = NULL, *dst_repo = NULL;
    SyncwDirent **src_dents = NULL, **dst_dents = NULL;
    char *src_canon_path = NULL, *dst_canon_path = NULL;
    SyncwCommit *dst_head_commit = NULL;
    int i = 0, ret = 0; 
    int file_num = 1; 
    gint64 *file_sizes = NULL;
    gboolean background = FALSE;
    char *task_id = NULL;
    char **src_names = NULL, **dst_names = NULL;
    SyncwerkCopyResult *res = NULL;

    GET_REPO_OR_FAIL(src_repo, src_repo_id);
    
    if (strcmp(src_repo_id, dst_repo_id) != 0) { 
        GET_REPO_OR_FAIL(dst_repo, dst_repo_id);

        if (src_repo->encrypted || dst_repo->encrypted) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                         "Can't copy files between encrypted repo(s)");
            ret = -1;
            goto out;
        }

    } else {
        syncw_repo_ref (src_repo);
        dst_repo = src_repo;
    }

    src_canon_path = get_canonical_path (src_path);
    dst_canon_path = get_canonical_path (dst_path);

    GET_COMMIT_OR_FAIL(dst_head_commit,
                       dst_repo->id, dst_repo->version,
                       dst_repo->head->commit_id);
    /*FAIL_IF_FILE_EXISTS(dst_repo->store_id, dst_repo->version,
                        dst_head_commit->root_id, dst_canon_path, dst_filename, NULL);*/

    /* get src dirents */
    if (strchr(src_filenames, '\t') && strchr(dst_filenames, '\t')) {
        src_names = g_strsplit (src_filenames, "\t", -1);
        dst_names = g_strsplit (dst_filenames, "\t", -1);
        file_num = g_strv_length (src_names);
        int dst_file_num = g_strv_length (dst_names);
        if (dst_file_num != file_num) {
            ret = -1;
            syncw_warning ("[move files] Bad args.\n");
            goto out;
        }

        src_dents = g_new0 (SyncwDirent *, file_num);
        file_sizes = g_new0 (gint64, file_num);

        for (i = 0; i < file_num; i++) {
            if (strcmp(src_names[i], "") == 0) {
                ret = -1;
                syncw_warning ("[move files] Bad args: Empty src_filenames.\n");
                goto out;
            }
            src_dents[i] = get_dirent_by_path (src_repo, NULL,
                                               src_canon_path, src_names[i], error);
            if (!src_dents[i]) {
                ret = -1;
                syncw_warning ("[move files] File %s not Found.\n", src_names[i]);
                goto out;
            }
            file_sizes[i] = (src_dents[i]->version > 0) ? src_dents[i]->size : -1;

        }
    } else {
        ret = -1;
        syncw_warning ("[move files] Bad args: Split filenames with '\\t'.\n");
        goto out;
    }
    
    /* move file within the same repo */
    if (src_repo == dst_repo) {
        dst_dents = g_new0 (SyncwDirent *, file_num);
        for (i = 0; i < file_num; i++) {
            if (strcmp(dst_names[i], "") == 0) {
                ret = -1;
                syncw_warning ("[move files] Bad args: Empty dst_filenames.\n");
                goto out;
            }
            /* duplicate src dirents with new names */
            dst_dents[i] = syncw_dirent_new (dir_version_from_repo_version (dst_repo->version),
                                            src_dents[i]->id, src_dents[i]->mode, dst_names[i],
                                            src_dents[i]->mtime, user, file_sizes[i]);
        }
        /* move file within the same repo */
        if (move_file_same_repo (src_repo_id,
                                 src_canon_path, src_dents,
                                 dst_canon_path, dst_dents,
                                 file_num, replace, user, error) < 0) {
            ret = -1;
            goto out;
        }
        syncw_repo_manager_cleanup_virtual_repos (mgr, src_repo_id);
        syncw_repo_manager_merge_virtual_repo (mgr, src_repo_id, NULL);

        update_repo_size (dst_repo_id);
    } else {
        /* move between virtual and origin repos */
        if (is_virtual_repo_and_origin (src_repo, dst_repo)) {
            dst_dents = g_new0 (SyncwDirent *, file_num);
            for (i = 0; i < file_num; i++) {
                dst_dents[i] = syncw_dirent_new (dir_version_from_repo_version(dst_repo->version),
                                                src_dents[i]->id, src_dents[i]->mode, dst_names[i],
                                                src_dents[i]->mtime, user, src_dents[i]->size);
            }
            if (put_dirent_and_commit (dst_repo,
                                       dst_path,
                                       dst_dents,
                                       file_num,
                                       replace,
                                       user,
                                       NULL) < 0) { 
                ret = -1;
                goto out; 
            }
            syncw_repo_manager_merge_virtual_repo (mgr, dst_repo->id, NULL);

            if (syncw_repo_manager_del_file (mgr, src_repo->id, src_path,
                                            src_filenames, user, error) < 0) { 
                ret = -1;
                goto out; 
            }    

            syncw_repo_manager_merge_virtual_repo (mgr, src_repo->id, NULL);

            update_repo_size (dst_repo->id);
        } else if (!synchronous) {
            /* move between different repos */
            background = TRUE;

            gint64 total_files = -1;
            gint64 total_files_all = 0;
            gint64 total_size_all = 0;

            for (i = 0; i < file_num; i++) {
                if (S_ISDIR(src_dents[i]->mode))
                    total_files = syncw_fs_manager_count_fs_files (syncw->fs_mgr,
                                                                  src_repo->store_id,
                                                                  src_repo->version,
                                                                  src_dents[i]->id);
                else
                    total_files = 1;
                if (total_files < 0) {
                    syncw_warning ("Failed to get file count.\n");
                    ret = -1;
                    goto out;
                }
                total_files_all += total_files;
                if (!check_file_count_and_size (src_repo, src_dents[i], total_files_all,
                                                &total_size_all, error)) {
                    ret = -1;
                    goto out;
                }
            }
            task_id = syncw_copy_manager_add_task (syncw->copy_mgr,
                                                  src_repo_id,
                                                  src_canon_path,
                                                  src_filenames,
                                                  dst_repo_id,
                                                  dst_canon_path,
                                                  dst_filenames,
                                                  0,
                                                  user,
                                                  total_files_all,
                                                  cross_repo_move,
                                                  need_progress);
            if (need_progress && !task_id) {
                syncw_warning ("Failed to start copy task.\n");
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                        "failed to start copy task");
                ret = -1;
                goto out; 
            }
        } else {
            /* Synchronous for cross-repo move */
            if (cross_repo_move (src_repo_id,
                                 src_canon_path,
                                 src_filenames,
                                 dst_repo_id,
                                 dst_canon_path,
                                 dst_filenames,
                                 replace,
                                 user,
                                 NULL) < 0) { 
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                             "Failed to move");
                ret = -1;
                goto out; 
            }
        } // Synchronous move
    } //else diffrent repo

out:
    if (src_repo) syncw_repo_unref (src_repo);
    if (dst_repo) syncw_repo_unref (dst_repo);

    if (dst_head_commit) syncw_commit_unref(dst_head_commit);
    
    if (src_canon_path) g_free (src_canon_path);
    if (dst_canon_path) g_free (dst_canon_path);

    if (src_names)
        g_strfreev (src_names);
    if (dst_names)
        g_strfreev (dst_names);
    if (file_sizes)
        g_free (file_sizes);
    if (src_dents) {
        for (i = 0; i < file_num; i++)
            syncw_dirent_free (src_dents[i]);
        g_free (src_dents);
    }
    if (dst_dents) {
        for (i = 0; i < file_num; i++)
            syncw_dirent_free (dst_dents[i]);
        g_free (dst_dents);
    }
    
    if (ret == 0) { 
        res = syncwerk_copy_result_new ();
        g_object_set (res, "background", background, "task_id", task_id, NULL);
        g_free (task_id);
    }    

    return res;
}

int
syncw_repo_manager_mkdir_with_parents (SyncwRepoManager *mgr,
                                      const char *repo_id,
                                      const char *parent_dir,
                                      const char *new_dir_path,
                                      const char *user,
                                      GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char **sub_folders = NULL;
    int nfolder;
    char buf[SYNCW_PATH_MAX];
    char *root_id = NULL;
    SyncwDirent *new_dent = NULL;
    char *parent_dir_can = NULL;
    char *relative_dir_can = NULL;
    char *abs_path = NULL;
    int total_path_len;
    int sub_folder_len;
    GList *uncre_dir_list = NULL;
    GList *iter_list = NULL;
    char *uncre_dir;
    int ret = 0; 

    if (new_dir_path[0] == '/' || new_dir_path[0] == '\\') {
        syncw_warning ("[mkdir with parent] Invalid relative path %s.\n", new_dir_path);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid relative path");
        return -1;
    }    

    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    relative_dir_can = get_canonical_path (new_dir_path);
    sub_folders = g_strsplit (relative_dir_can, "/", 0);
    nfolder = g_strv_length (sub_folders);

    int i = 0;
    for (; i < nfolder; ++i) {
        if (strcmp (sub_folders[i], "") == 0)
            continue;

        if (should_ignore_file (sub_folders[i], NULL)) {
            syncw_warning ("[post dir] Invalid dir name %s.\n", sub_folders[i]);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                         "Invalid dir name");
            ret = -1;
            goto out;
        }
    }

    if (strcmp (parent_dir, "/") == 0 ||
        strcmp (parent_dir, "\\") == 0) {
        parent_dir_can = g_strdup ("/");
        abs_path = g_strdup_printf ("%s%s", parent_dir_can, relative_dir_can);
    } else {
        parent_dir_can = get_canonical_path (parent_dir);
        abs_path = g_strdup_printf ("%s/%s", parent_dir_can, relative_dir_can);
    }
    if (!abs_path) {
        syncw_warning ("[mkdir with parent] Out of memory.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "Out of memory");
        ret = -1;
        goto out;
    }
    total_path_len = strlen (abs_path);

    // from the last, to check the folder exist
    i = nfolder - 1;
    for (; i >= 0; --i) {
        if (strcmp (sub_folders[i], "") == 0)
            continue;

        sub_folder_len = strlen (sub_folders[i]) + 1;
        total_path_len -= sub_folder_len;
        memset (abs_path + total_path_len, '\0', sub_folder_len);

        if (check_file_exists (repo->store_id, repo->version,
                               head_commit->root_id, abs_path, sub_folders[i], NULL)) {
            // folder exist, skip loop to create unexist subfolder
            strcat (abs_path, "/");
            strcat (abs_path, sub_folders[i]);
            break;
        } else {
            // folder not exist, cache it to create later
            uncre_dir_list = g_list_prepend (uncre_dir_list, sub_folders[i]);
        }
    }

    if (uncre_dir_list) {
        // exist parent folder has been found, based on it to create unexist subfolder
        char new_root_id[41];
        memcpy (new_root_id, head_commit->root_id, 40);
        new_root_id[40] = '\0';

        for (iter_list = uncre_dir_list; iter_list; iter_list = iter_list->next) {
            uncre_dir = iter_list->data;
            new_dent = syncw_dirent_new (dir_version_from_repo_version(repo->version),
                                        EMPTY_SHA1, S_IFDIR, uncre_dir,
                                        (gint64)time(NULL), NULL, -1);

            root_id = do_post_file (repo,
                                    new_root_id, abs_path, new_dent);
            if (!root_id) {
                syncw_warning ("[put dir] Failed to put dir.\n");
                g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                             "Failed to put dir");
                ret = -1;
                syncw_dirent_free (new_dent);
                goto out;
            }

            // the last folder has been created
            if (!iter_list->next) {
                syncw_dirent_free (new_dent);
                break;
            }

            strcat (abs_path, "/");
            strcat (abs_path, uncre_dir);
            memcpy (new_root_id, root_id, 40);

            syncw_dirent_free (new_dent);
            g_free (root_id);
        }

        /* Commit. */
        snprintf(buf, SYNCW_PATH_MAX, "Added directory \"%s\"", relative_dir_can);
        if (gen_new_commit (repo_id, head_commit, root_id,
                            user, buf, NULL, error) < 0) {
            ret = -1;
            g_free (root_id);
            goto out;
        }

        syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);
        g_free (root_id);
    }

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref(head_commit);
    if (sub_folders)
        g_strfreev (sub_folders);
    if (uncre_dir_list)
        g_list_free (uncre_dir_list);
    if (relative_dir_can)
        g_free (relative_dir_can);
    if (parent_dir_can)
        g_free (parent_dir_can);
    if (abs_path)
        g_free (abs_path);

    return ret;
}

int
syncw_repo_manager_post_dir (SyncwRepoManager *mgr,
                            const char *repo_id,
                            const char *parent_dir,
                            const char *new_dir_name,
                            const char *user,
                            GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char *canon_path = NULL;
    char buf[SYNCW_PATH_MAX];
    char *root_id = NULL;
    SyncwDirent *new_dent = NULL;
    int ret = 0;

    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    canon_path = get_canonical_path (parent_dir);

    if (should_ignore_file (new_dir_name, NULL)) {
        syncw_warning ("[post dir] Invalid dir name %s.\n", new_dir_name);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid dir name");
        ret = -1;
        goto out;
    }

    FAIL_IF_FILE_EXISTS(repo->store_id, repo->version,
                        head_commit->root_id, canon_path, new_dir_name, NULL);

    if (!new_dent) {
        new_dent = syncw_dirent_new (dir_version_from_repo_version(repo->version),
                                    EMPTY_SHA1, S_IFDIR, new_dir_name,
                                    (gint64)time(NULL), NULL, -1);
    }

    root_id = do_post_file (repo,
                            head_commit->root_id, canon_path, new_dent);
    if (!root_id) {
        syncw_warning ("[put dir] Failed to put dir %s to %s in repo %s.\n",
                      new_dir_name, canon_path, repo->id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to put dir");
        ret = -1;
        goto out;
    }

    /* Commit. */
    snprintf(buf, SYNCW_PATH_MAX, "Added directory \"%s\"", new_dir_name);
    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, buf, NULL, error) < 0) {
        ret = -1;
        goto out;
    }

    syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref(head_commit);
    syncw_dirent_free (new_dent);
    g_free (root_id);
    g_free (canon_path);

    return ret;
}

int
syncw_repo_manager_post_empty_file (SyncwRepoManager *mgr,
                                   const char *repo_id,
                                   const char *parent_dir,
                                   const char *new_file_name,
                                   const char *user,
                                   GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char *canon_path = NULL;
    char buf[SYNCW_PATH_MAX];
    char *root_id = NULL;
    SyncwDirent *new_dent = NULL;
    int ret = 0;

    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    if (!canon_path)
        /* no need to call get_canonical_path again when retry */
        canon_path = get_canonical_path (parent_dir);

    if (should_ignore_file (new_file_name, NULL)) {
        syncw_warning ("[post file] Invalid file name %s.\n", new_file_name);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid file name");
        ret = -1;
        goto out;
    }

    FAIL_IF_FILE_EXISTS(repo->store_id, repo->version,
                        head_commit->root_id, canon_path, new_file_name, NULL);

    if (!new_dent) {
        new_dent = syncw_dirent_new (dir_version_from_repo_version(repo->version),
                                    EMPTY_SHA1, STD_FILE_MODE, new_file_name,
                                    (gint64)time(NULL), user, 0);
    }

    root_id = do_post_file (repo,
                            head_commit->root_id, canon_path, new_dent);
    if (!root_id) {
        syncw_warning ("[put dir] Failed to create empty file dir.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to put dir");
        ret = -1;
        goto out;
    }

    /* Commit. */
    snprintf(buf, SYNCW_PATH_MAX, "Added \"%s\"", new_file_name);
    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, buf, NULL, error) < 0) {
        ret = -1;
        goto out;
    }

    syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);

    update_repo_size (repo_id);

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref(head_commit);
    syncw_dirent_free (new_dent);
    g_free (root_id);
    g_free (canon_path);

    return ret;
}

static char *
rename_file_recursive(SyncwRepo *repo,
                      const char *dir_id,
                      const char *to_path,
                      const char *oldname,
                      const char *newname)
{
    SyncwDir *olddir, *newdir;
    SyncwDirent *dent;
    GList *ptr;
    char *to_path_dup = NULL;
    char *remain = NULL;
    char *slash;
    char *id = NULL;
    char *ret = NULL;

    olddir = syncw_fs_manager_get_syncwdir_sorted(syncw->fs_mgr,
                                                repo->store_id, repo->version,
                                                dir_id);
    if (!olddir)
        return NULL;

    /* we reach the target dir. */
    if (*to_path == '\0') {
        SyncwDirent *old, *newdent = NULL;
        GList *newentries = NULL, *p;

        /* When renameing, there is a pitfall: we can't simply rename the
         * dirent, since the dirents are required to be sorted in descending
         * order. We need to copy all old dirents except the target dirent,
         * and then rename the target dirent, and then insert the new
         * dirent, so that we can maintain the descending order of dirents. */
        for (p = olddir->entries; p != NULL; p = p->next) {
            old = p->data;
            if (strcmp(old->name, oldname) != 0) {
                newentries = g_list_prepend (newentries, syncw_dirent_dup(old));
            } else {
                newdent = syncw_dirent_new (old->version, old->id, old->mode,
                                           newname, old->mtime,
                                           old->modifier, old->size);
            }
        }

        newentries = g_list_reverse (newentries);

        if (newdent) {
            newentries = g_list_insert_sorted(newentries, newdent, compare_dirents);
        }

        newdir = syncw_dir_new (NULL, newentries,
                               dir_version_from_repo_version(repo->version));
        if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strndup (newdir->dir_id, 40);
        syncw_dir_free (newdir);

        goto out;
    }

    to_path_dup = g_strdup (to_path);
    slash = strchr (to_path_dup, '/');

    if (!slash) {
        remain = to_path_dup + strlen(to_path_dup);
    } else {
        *slash = '\0';
        remain = slash + 1;
    }

    for (ptr = olddir->entries; ptr; ptr = ptr->next) {
        dent = (SyncwDirent *)ptr->data;

        if (strcmp(dent->name, to_path_dup) != 0)
            continue;

        id = rename_file_recursive (repo, dent->id, remain, oldname, newname);
        if (id != NULL) {
            memcpy(dent->id, id, 40);
            dent->id[40] = '\0';
        }
        break;
    }
    
    if (id != NULL) {
        /* Create a new SyncwDir. */
        GList *new_entries;
        
        new_entries = dup_syncwdir_entries (olddir->entries);
        newdir = syncw_dir_new (NULL, new_entries,
                               dir_version_from_repo_version(repo->version));
        if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strdup(newdir->dir_id);
        syncw_dir_free (newdir);
    }

out:
    g_free (to_path_dup);
    g_free (id);
    syncw_dir_free(olddir);
    return ret;
}

static char *
do_rename_file(SyncwRepo *repo,
               const char *root_id,
               const char *parent_dir,
               const char *oldname,
               const char *newname)
{
    /* if parent_dir is a absolutely path, we will remove the first '/' */
    if (*parent_dir == '/')
        parent_dir = parent_dir + 1;

    return rename_file_recursive(repo, root_id, parent_dir, oldname, newname);
}


int
syncw_repo_manager_rename_file (SyncwRepoManager *mgr,
                               const char *repo_id,
                               const char *parent_dir,
                               const char *oldname,
                               const char *newname,
                               const char *user,
                               GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char *root_id = NULL;
    char *canon_path = NULL;
    char buf[SYNCW_PATH_MAX];
    int mode = 0;
    int ret = 0;

    if (strcmp(oldname, newname) == 0)
        return 0;
    
    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);
    
    if (!canon_path)
        canon_path = get_canonical_path (parent_dir);

    if (should_ignore_file (newname, NULL)) {
        syncw_warning ("[rename file] Invalid filename %s.\n", newname);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid filename");
        ret = -1;
        goto out;
    }

    FAIL_IF_FILE_NOT_EXISTS(repo->store_id, repo->version,
                            head_commit->root_id, canon_path, oldname, &mode);
    FAIL_IF_FILE_EXISTS(repo->store_id, repo->version,
                        head_commit->root_id, canon_path, newname, NULL);

    root_id = do_rename_file (repo, head_commit->root_id, canon_path,
                              oldname, newname);
    if (!root_id) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "faile to rename file %s", oldname);
        ret = -1;
        goto out;
    }

    /* Commit. */
    if (S_ISDIR(mode)) {
        snprintf(buf, SYNCW_PATH_MAX, "Renamed directory \"%s\"", oldname);
    } else {
        snprintf(buf, SYNCW_PATH_MAX, "Renamed \"%s\"", oldname);
    }

    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, buf, NULL, error) < 0) {
        ret = -1;
        goto out;
    }

    syncw_repo_manager_cleanup_virtual_repos (mgr, repo_id);
    syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref (head_commit);
    g_free (canon_path);
    g_free (root_id);

    return ret;
}

static char *
put_file_recursive(SyncwRepo *repo,
                   const char *dir_id,
                   const char *to_path,
                   SyncwDirent *newdent)
{
    SyncwDir *olddir, *newdir;
    SyncwDirent *dent;
    GList *ptr;
    char *to_path_dup = NULL;
    char *remain = NULL;
    char *slash;
    char *id = NULL;
    char *ret = NULL;

    olddir = syncw_fs_manager_get_syncwdir_sorted(syncw->fs_mgr,
                                                repo->store_id, repo->version,
                                                dir_id);
    if (!olddir)
        return NULL;

    /* we reach the target dir. Update the target dirent. */
    if (*to_path == '\0') {
        GList *newentries = NULL, *p;
        SyncwDirent *dent;

        for (p = olddir->entries; p; p = p->next) {
            dent = p->data;
            if (strcmp(dent->name, newdent->name) == 0) {
                newentries = g_list_prepend (newentries, syncw_dirent_dup(newdent));
            } else {
                newentries = g_list_prepend (newentries, syncw_dirent_dup(dent));
            }
        }

        newentries = g_list_reverse (newentries);
        newdir = syncw_dir_new (NULL, newentries,
                               dir_version_from_repo_version(repo->version));
        if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strdup (newdir->dir_id);
        syncw_dir_free (newdir);

        goto out;
    }

    to_path_dup = g_strdup (to_path);
    slash = strchr (to_path_dup, '/');

    if (!slash) {
        remain = to_path_dup + strlen(to_path_dup);
    } else {
        *slash = '\0';
        remain = slash + 1;
    }

    for (ptr = olddir->entries; ptr; ptr = ptr->next) {
        dent = (SyncwDirent *)ptr->data;

        if (strcmp(dent->name, to_path_dup) != 0)
            continue;

        id = put_file_recursive (repo, dent->id, remain, newdent);
        if (id != NULL) {
            memcpy(dent->id, id, 40);
            dent->id[40] = '\0';
            if (repo->version > 0)
                dent->mtime = (guint64)time(NULL);
        }
        break;
    }
    
    if (id != NULL) {
        /* Create a new SyncwDir. */
        GList *new_entries;
        
        new_entries = dup_syncwdir_entries (olddir->entries);
        newdir = syncw_dir_new (NULL, new_entries,
                               dir_version_from_repo_version(repo->version));
        if (syncw_dir_save (syncw->fs_mgr, repo->store_id, repo->version, newdir) == 0)
            ret = g_strdup(newdir->dir_id);
        syncw_dir_free (newdir);
    }

out:
    g_free (to_path_dup);
    g_free (id);
    syncw_dir_free(olddir);
    return ret;
}

static char *
do_put_file (SyncwRepo *repo,
             const char *root_id,
             const char *parent_dir,
             SyncwDirent *dent)
{
    /* if parent_dir is a absolutely path, we will remove the first '/' */
    if (*parent_dir == '/')
        parent_dir = parent_dir + 1;

    return put_file_recursive(repo, root_id, parent_dir, dent);
}

int
syncw_repo_manager_put_file (SyncwRepoManager *mgr,
                            const char *repo_id,
                            const char *temp_file_path,
                            const char *parent_dir,
                            const char *file_name,
                            const char *user,
                            const char *head_id,
                            char **new_file_id,
                            GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char *canon_path = NULL;
    unsigned char sha1[20];
    char buf[SYNCW_PATH_MAX];
    char *root_id = NULL;
    SyncwerkCrypt *crypt = NULL;
    SyncwDirent *new_dent = NULL;
    char hex[41];
    char *old_file_id = NULL, *fullpath = NULL;
    int ret = 0;

    if (g_access (temp_file_path, R_OK) != 0) {
        syncw_warning ("[put file] File %s doesn't exist or not readable.\n",
                      temp_file_path);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid input file");
        return -1;
    }

    GET_REPO_OR_FAIL(repo, repo_id);
    const char *base = head_id ? head_id : repo->head->commit_id;
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, base);

    if (!canon_path)
        canon_path = get_canonical_path (parent_dir);

    if (should_ignore_file (file_name, NULL)) {
        syncw_warning ("[put file] Invalid filename %s.\n", file_name);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid filename");
        ret = -1;
        goto out;
    }

    if (strstr (parent_dir, "//") != NULL) {
        syncw_warning ("[put file] parent_dir cantains // sequence.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS,
                     "Invalid parent dir");
        ret = -1;
        goto out;
    }
    
    FAIL_IF_FILE_NOT_EXISTS(repo->store_id, repo->version,
                            head_commit->root_id, canon_path, file_name, NULL);

    /* Write blocks. */
    if (repo->encrypted) {
        unsigned char key[32], iv[16];
        if (syncw_passwd_manager_get_decrypt_key_raw (syncw->passwd_mgr,
                                                     repo_id, user,
                                                     key, iv) < 0) {
            syncw_warning ("Passwd for repo %s is not set.\n", repo_id);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "Passwd is not set");
            ret = -1;
            goto out;
        }
        crypt = syncwerk_crypt_new (repo->enc_version, key, iv);
    }

    gint64 size;
    if (syncw_fs_manager_index_blocks (syncw->fs_mgr,
                                      repo->store_id, repo->version,
                                      temp_file_path,
                                      sha1, &size, crypt, TRUE, FALSE, NULL) < 0) {
        syncw_warning ("failed to index blocks");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to index blocks");
        ret = -1;
        goto out;
    }
        
    rawdata_to_hex(sha1, hex, 20);
    new_dent = syncw_dirent_new (dir_version_from_repo_version(repo->version),
                                hex, STD_FILE_MODE, file_name,
                                (gint64)time(NULL), user, size);

    if (!fullpath)
        fullpath = g_build_filename(parent_dir, file_name, NULL);

    old_file_id = syncw_fs_manager_path_to_obj_id (syncw->fs_mgr,
                                                  repo->store_id, repo->version,
                                                  head_commit->root_id,
                                                  fullpath, NULL, NULL);

    if (g_strcmp0(old_file_id, new_dent->id) == 0) {
        if (new_file_id)
            *new_file_id = g_strdup(new_dent->id);
        goto out;
    }

    root_id = do_put_file (repo, head_commit->root_id, canon_path, new_dent);
    if (!root_id) {
        syncw_warning ("[put file] Failed to put file %s to %s in repo %s.\n",
                      file_name, canon_path, repo->id);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to put file");
        ret = -1;
        goto out;
    }

    /* Commit. */
    snprintf(buf, SYNCW_PATH_MAX, "Modified \"%s\"", file_name);
    if (gen_new_commit (repo_id, head_commit, root_id, user, buf, NULL, error) < 0) {
        ret = -1;
        goto out;       
    }

    if (new_file_id)
        *new_file_id = g_strdup(new_dent->id);

    syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref(head_commit);
    syncw_dirent_free (new_dent);
    g_free (root_id);
    g_free (canon_path);
    g_free (crypt);
    g_free (old_file_id);
    g_free (fullpath);

    if (ret == 0) {
        update_repo_size (repo_id);
    }

    return ret;
}

static char *
gen_commit_description (SyncwRepo *repo,
                        const char *root,
                        const char *parent_root)
{
    GList *p;
    GList *results = NULL;
    char *desc;
    
    diff_commit_roots (repo->store_id, repo->version,
                       parent_root, root, &results, TRUE);

    desc = diff_results_to_description (results);

    for (p = results; p; p = p->next) {
        DiffEntry *de = p->data;
        diff_entry_free (de);
    }
    g_list_free (results);

    return desc;
}

int
syncw_repo_manager_update_dir (SyncwRepoManager *mgr,
                              const char *repo_id,
                              const char *dir_path,
                              const char *new_dir_id,
                              const char *user,
                              const char *head_id,
                              char *new_commit_id,
                              GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    char *canon_path = NULL;
    char *parent = NULL, *dirname = NULL;
    SyncwDirent *new_dent = NULL;
    char *root_id = NULL;
    char *commit_desc = NULL;
    int ret = 0;

    GET_REPO_OR_FAIL(repo, repo_id);
    const char *base = head_id ? head_id : repo->head->commit_id;
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, base);

    /* Are we updating the root? */
    if (strcmp (dir_path, "/") == 0) {
        commit_desc = gen_commit_description (repo, new_dir_id, head_commit->root_id);
        if (!commit_desc)
            commit_desc = g_strdup("Auto merge by system");

        if (gen_new_commit (repo_id, head_commit, new_dir_id,
                            user, commit_desc, new_commit_id, error) < 0)
            ret = -1;
        g_free (commit_desc);
        goto out;
    }

    parent = g_path_get_dirname (dir_path);
    canon_path = get_canonical_path (parent);
    g_free (parent);

    dirname = g_path_get_basename (dir_path);

    FAIL_IF_FILE_NOT_EXISTS(repo->store_id, repo->version,
                            head_commit->root_id, canon_path, dirname, NULL);

    new_dent = syncw_dirent_new (dir_version_from_repo_version(repo->version),
                                new_dir_id, S_IFDIR, dirname,
                                (gint64)time(NULL), NULL, -1);

    root_id = do_put_file (repo, head_commit->root_id, canon_path, new_dent);
    if (!root_id) {
        syncw_warning ("[update dir] Failed to put file.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to update dir");
        ret = -1;
        goto out;
    }

    commit_desc = gen_commit_description (repo, root_id, head_commit->root_id);
    if (!commit_desc)
        commit_desc = g_strdup("Auto merge by system");

    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, commit_desc, new_commit_id, error) < 0) {
        ret = -1;
        g_free (commit_desc);
        goto out;
    }
    g_free (commit_desc);

out:
    syncw_repo_unref (repo);
    syncw_commit_unref (head_commit);
    syncw_dirent_free (new_dent);
    g_free (canon_path);
    g_free (dirname);
    g_free (root_id);

    if (ret == 0)
        update_repo_size (repo_id);

    return ret;
}

/* int */
/* syncw_repo_manager_put_file_blocks (SyncwRepoManager *mgr, */
/*                                    const char *repo_id, */
/*                                    const char *parent_dir, */
/*                                    const char *file_name, */
/*                                    const char *blockids_json, */
/*                                    const char *paths_json, */
/*                                    const char *user, */
/*                                    const char *head_id, */
/*                                    gint64 file_size, */
/*                                    char **new_file_id, */
/*                                    GError **error) */
/* { */
/*     SyncwRepo *repo = NULL; */
/*     SyncwCommit *head_commit = NULL; */
/*     char *canon_path = NULL; */
/*     unsigned char sha1[20]; */
/*     char buf[SYNCW_PATH_MAX]; */
/*     char *root_id = NULL; */
/*     SyncwDirent *new_dent = NULL; */
/*     char hex[41]; */
/*     GList *blockids = NULL, *paths = NULL, *ptr; */
/*     char *old_file_id = NULL, *fullpath = NULL; */
/*     int ret = 0; */

/*     blockids = json_to_file_list (blockids_json); */
/*     paths = json_to_file_list (paths_json); */
/*     if (g_list_length(blockids) != g_list_length(paths)) { */
/*         syncw_warning ("[put-blks] Invalid blockids or paths.\n"); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, "Invalid files"); */
/*         ret = -1; */
/*         goto out; */
/*     } */


/*     for (ptr = paths; ptr; ptr = ptr->next) { */
/*         char *temp_file_path = ptr->data; */
/*         if (g_access (temp_file_path, R_OK) != 0) { */
/*             syncw_warning ("[put-blks] File block %s doesn't exist or not readable.\n", */
/*                           temp_file_path); */
/*             g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                          "Invalid input file"); */
/*             ret = -1; */
/*             goto out; */
/*         } */
/*     } */

/*     GET_REPO_OR_FAIL(repo, repo_id); */
/*     const char *base = head_id ? head_id : repo->head->commit_id; */
/*     GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, base); */

/*     if (!canon_path) */
/*         canon_path = get_canonical_path (parent_dir); */

/*     if (should_ignore_file (file_name, NULL)) { */
/*         syncw_warning ("[put-blks] Invalid filename %s.\n", file_name); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Invalid filename"); */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     if (strstr (parent_dir, "//") != NULL) { */
/*         syncw_warning ("[put-blks] parent_dir cantains // sequence.\n"); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_ARGS, */
/*                      "Invalid parent dir"); */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     FAIL_IF_FILE_NOT_EXISTS(repo->store_id, repo->version, */
/*                             head_commit->root_id, canon_path, file_name, NULL); */

/*     /\* Write blocks. *\/ */
/*     if (syncw_fs_manager_index_file_blocks (syncw->fs_mgr, */
/*                                            repo->store_id, repo->version, */
/*                                            paths, */
/*                                            blockids, sha1, file_size) < 0) { */
/*         syncw_warning ("failed to index blocks"); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, */
/*                      "Failed to index blocks"); */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     rawdata_to_hex(sha1, hex, 20); */
/*     new_dent = syncw_dirent_new (dir_version_from_repo_version(repo->version), */
/*                                 hex, STD_FILE_MODE, file_name, */
/*                                 (gint64)time(NULL), user, file_size); */

/*     if (!fullpath) */
/*         fullpath = g_build_filename(parent_dir, file_name, NULL); */

/*     old_file_id = syncw_fs_manager_path_to_obj_id (syncw->fs_mgr, */
/*                                                   repo->store_id, repo->version, */
/*                                                   head_commit->root_id, */
/*                                                   fullpath, NULL, NULL); */

/*     if (g_strcmp0(old_file_id, new_dent->id) == 0) { */
/*         if (new_file_id) */
/*             *new_file_id = g_strdup(new_dent->id); */
/*         goto out; */
/*     } */

/*     root_id = do_put_file (repo, head_commit->root_id, canon_path, new_dent); */
/*     if (!root_id) { */
/*         syncw_warning ("[put-blks] Failed to put file.\n"); */
/*         g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL, */
/*                      "Failed to put file"); */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     /\* Commit. *\/ */
/*     snprintf(buf, SYNCW_PATH_MAX, "Modified \"%s\"", file_name); */
/*     if (gen_new_commit (repo_id, head_commit, root_id, user, buf, NULL, error) < 0) { */
/*         ret = -1; */
/*         goto out; */
/*     } */

/*     if (new_file_id) */
/*         *new_file_id = g_strdup(new_dent->id); */

/* out: */
/*     if (repo) */
/*         syncw_repo_unref (repo); */
/*     if (head_commit) */
/*         syncw_commit_unref(head_commit); */
/*     string_list_free (blockids); */
/*     string_list_free (paths); */
/*     syncw_dirent_free (new_dent); */
/*     g_free (root_id); */
/*     g_free (canon_path); */
/*     g_free (old_file_id); */
/*     g_free (fullpath); */

/*     if (ret == 0) { */
/*         update_repo_size (repo_id); */
/*     } */

/*     return ret; */
/* } */

/* split filename into base and extension */
static void
filename_splitext (const char *filename,
                   char **base,
                   char **ext)
{
    char *dot = strrchr(filename, '.');
    if (!dot) {
        *base = g_strdup(filename);
        *ext = NULL;
    } else {
        *dot = '\0';
        *base = g_strdup(filename);
        *dot = '.';

        *ext = g_strdup(dot);
    }
}

static char *
revert_file_to_root (SyncwRepo *repo,
                     const char *root_id,
                     SyncwDirent *old_dent,
                     gboolean *skipped,
                     GError **error)
{
    SyncwDir *dir = NULL;
    SyncwDirent *dent = NULL, *newdent = NULL;
    char *basename = NULL, *ext = NULL;
    char new_file_name[SYNCW_PATH_MAX];
    char *new_root_id = NULL;
    int i = 1;
    GList *p;

    *skipped = FALSE;

    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               repo->store_id, repo->version,
                                               root_id,
                                               "/", error);
    if (*error) {
        return NULL;
    }

    snprintf (new_file_name, sizeof(new_file_name), "%s", old_dent->name);

    filename_splitext(old_dent->name, &basename, &ext);
    for (;;) {
        for (p = dir->entries; p; p = p->next) {
            dent = p->data;
            if (strcmp(dent->name, new_file_name) != 0)
                continue;

            if (S_ISREG(dent->mode)) {
                /* same named file */
                if (strcmp(dent->id, old_dent->id) == 0) {
                    *skipped = TRUE;
                    goto out;
                } else {
                    /* rename and retry */
                    snprintf (new_file_name, sizeof(new_file_name), "%s (%d)%s",
                              basename, i++, ext);
                    break;
                }
                
            } else if (S_ISDIR(dent->mode)) {
                /* rename and retry */
                snprintf (new_file_name, sizeof(new_file_name), "%s (%d)%s",
                          basename, i++, ext);
                break;
            }
        }

        if (p == NULL)
            break;
    }

    newdent = syncw_dirent_new (old_dent->version,
                               old_dent->id, STD_FILE_MODE, new_file_name,
                               old_dent->mtime, old_dent->modifier, old_dent->size);
    new_root_id = do_post_file (repo, root_id, "/", newdent);

out:
    if (dir)
        syncw_dir_free (dir);

    g_free (basename);
    g_free (ext);
    syncw_dirent_free (newdent);

    return new_root_id;
}

static char *
revert_file_to_parent_dir (SyncwRepo *repo,
                           const char *root_id,
                           const char *parent_dir,
                           SyncwDirent *old_dent,
                           gboolean *skipped,
                           GError **error)
{
    SyncwDir *dir = NULL;
    SyncwDirent *dent = NULL, *newdent = NULL;
    char *basename = NULL, *ext = NULL;
    char new_file_name[SYNCW_PATH_MAX];
    char *new_root_id = NULL;
    gboolean is_overwrite = FALSE;
    int i = 1;
    GList *p;
    
    *skipped = FALSE;

    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               repo->store_id, repo->version,
                                               root_id,
                                               parent_dir, error);
    if (*error) {
        return NULL;
    }

    snprintf (new_file_name, sizeof(new_file_name), "%s", old_dent->name);
    filename_splitext(old_dent->name, &basename, &ext);
    while(TRUE) {
        for (p = dir->entries; p; p = p->next) {
            dent = p->data;
            if (strcmp(dent->name, new_file_name) != 0)
                continue;

            if (S_ISREG(dent->mode)) {
                /* same named file */
                if (strcmp(dent->id, old_dent->id) == 0) {
                    *skipped = TRUE;
                    goto out;
                } else {
                    /* same name, different id: just overwrite */
                    is_overwrite = TRUE;
                    goto do_revert;
                }
                
            } else if (S_ISDIR(dent->mode)) {
                /* rename and retry */
                snprintf (new_file_name, sizeof(new_file_name), "%s (%d)%s",
                          basename, i++, ext);
                break;
            }
        }

        if (p == NULL)
            break;
    }

do_revert:    
    newdent = syncw_dirent_new (old_dent->version,
                               old_dent->id, STD_FILE_MODE, new_file_name,
                               old_dent->mtime, old_dent->modifier, old_dent->size);
    if (is_overwrite) {
        new_root_id = do_put_file (repo,
                                   root_id, parent_dir, newdent);
    } else {
        new_root_id = do_post_file (repo,
                                    root_id, parent_dir, newdent);
    }

out:
    if (dir)
        syncw_dir_free (dir);

    g_free (basename);
    g_free (ext);
    syncw_dirent_free (newdent);

    return new_root_id;
}

static gboolean
detect_path_exist (SyncwRepo *repo,
                   const char *root_id,
                   const char *path,
                   GError **error)
{
    SyncwDir *dir;

    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               repo->store_id, repo->version,
                                               root_id, path, error);
    if (*error) {
        if (g_error_matches(*error, SYNCWERK_DOMAIN, SYNCW_ERR_PATH_NO_EXIST)) {
            /* path does not exist */
            g_clear_error(error);
            return FALSE;
        } else {
            /* Other error */
            return FALSE;
        }
    }

    syncw_dir_free(dir);
    return TRUE;
}

int
syncw_repo_manager_revert_file (SyncwRepoManager *mgr,
                               const char *repo_id,
                               const char *old_commit_id,
                               const char *file_path,
                               const char *user,
                               GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL, *old_commit = NULL;
    char *parent_dir = NULL, *filename = NULL;
    SyncwDirent *old_dent = NULL;
    char *canon_path = NULL, *root_id = NULL;
    char buf[SYNCW_PATH_MAX];
    char time_str[512];
    gboolean parent_dir_exist = FALSE;
    gboolean revert_to_root = FALSE;
    gboolean skipped = FALSE;
    int ret = 0;

    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    /* If old_commit_id is head commit, do nothing. */
    if (strcmp(repo->head->commit_id, old_commit_id) == 0) {
        g_debug ("[revert file] commit is head, do nothing\n");
        goto out;
    }

    if (!old_commit) {
        GET_COMMIT_OR_FAIL(old_commit, repo->id, repo->version, old_commit_id);
        if (strcmp(old_commit->repo_id, repo_id) != 0) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_COMMIT,
                         "bad commit id");
            ret = -1;
            goto out;
        }
    }

    if (!canon_path) {
        canon_path = get_canonical_path (file_path);
        if (canon_path[strlen(canon_path) -1 ] == '/') {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_COMMIT,
                         "bad target file path");
            ret = -1;
            goto out;
        }

        parent_dir  = g_path_get_dirname(canon_path);
        filename = g_path_get_basename(canon_path);

        old_dent = get_dirent_by_path (repo, old_commit->root_id,
                                       parent_dir, filename, error);
        if (!old_dent || S_ISDIR(old_dent->mode)) {
            ret = -1;
            goto out;
        }
        if (*error) {
            syncw_warning ("[revert file] error: %s\n", (*error)->message);
            g_clear_error (error);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "internal error");
            ret = -1;
            goto out;
        }
    }

    parent_dir_exist = detect_path_exist (repo,
                                          head_commit->root_id,
                                          parent_dir, error);
    if (*error) {
        syncw_warning ("[revert file] error: %s\n", (*error)->message);
        g_clear_error (error);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "internal error");
        ret = -1;
        goto out;
    }
    
    if (!parent_dir_exist) {
        /* When parent dir does not exist, revert this file to root dir. */
        revert_to_root = TRUE;
        root_id = revert_file_to_root (repo,
                                       head_commit->root_id,
                                       old_dent,
                                       &skipped, error);
    } else {
        revert_to_root = FALSE;
        root_id = revert_file_to_parent_dir (repo,
                                             head_commit->root_id, parent_dir,
                                             old_dent,
                                             &skipped, error);
    }

    if (*error) {
        syncw_warning ("[revert file] error: %s\n", (*error)->message);
        g_clear_error (error);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "internal error");
        ret = -1;
        goto out;
    }

    if (skipped) {
        goto out;
    }
    
    if (!root_id) {
        syncw_warning ("[revert file] Failed to revert file.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to revert file");
        ret = -1;
        goto out;
    }

    /* Commit. */
#ifndef WIN32
    strftime (time_str, sizeof(time_str), "%F %T",
              localtime((time_t *)(&old_commit->ctime)));
#else
    strftime (time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
              localtime((time_t *)(&old_commit->ctime)));
#endif
    snprintf(buf, SYNCW_PATH_MAX, "Reverted file \"%s\" to status at %s", filename, time_str);
    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, buf, NULL, error) < 0) {
        ret = -1;
        goto out;
    }

    syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref (head_commit);
    if (old_commit)
        syncw_commit_unref (old_commit);

    g_free (root_id);
    g_free (parent_dir);
    g_free (filename);

    g_free (canon_path);
    syncw_dirent_free (old_dent);

#define REVERT_TO_ROOT              0x1
    if (ret == 0) {
        if (revert_to_root)
            ret |= REVERT_TO_ROOT;

        update_repo_size (repo_id);
    }

    return ret;
}

static char *
revert_dir (SyncwRepo *repo,
            const char *root_id,
            const char *parent_dir,
            SyncwDirent *old_dent,
            gboolean *skipped,
            GError **error)
{
    SyncwDir *dir = NULL;
    SyncwDirent *dent = NULL, *newdent = NULL;
    char new_dir_name[SYNCW_PATH_MAX];
    char *new_root_id = NULL;
    int i = 1;
    GList *p;

    *skipped = FALSE;

    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               repo->store_id, repo->version,
                                               root_id,
                                               parent_dir, error);
    if (*error) {
        return NULL;
    }

    snprintf (new_dir_name, sizeof(new_dir_name), "%s", old_dent->name);

    for (;;) {
        for (p = dir->entries; p; p = p->next) {
            dent = p->data;
            if (strcmp(dent->name, new_dir_name) != 0)
                continue;

            /* the same dir */
            if (S_ISDIR(dent->mode) && strcmp(dent->id, old_dent->id) == 0) {
                *skipped = TRUE;
                goto out;
            } else {
                /* rename and retry */
                snprintf (new_dir_name, sizeof(new_dir_name), "%s (%d)",
                          old_dent->name, i++);
                break;
            }
        }

        if (p == NULL)
            break;
    }

    newdent = syncw_dirent_new (old_dent->version,
                               old_dent->id, S_IFDIR, new_dir_name,
                               old_dent->mtime, NULL, -1);
    new_root_id = do_post_file (repo, root_id, parent_dir, newdent);

out:
    if (dir)
        syncw_dir_free (dir);

    syncw_dirent_free (newdent);

    return new_root_id;
}

int
syncw_repo_manager_revert_dir (SyncwRepoManager *mgr,
                              const char *repo_id,
                              const char *old_commit_id,
                              const char *dir_path,
                              const char *user,
                              GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL, *old_commit = NULL;
    char *parent_dir = NULL, *dirname = NULL;
    SyncwDirent *old_dent = NULL;
    char *canon_path = NULL, *root_id = NULL;
    char buf[SYNCW_PATH_MAX];
    gboolean parent_dir_exist = FALSE;
    gboolean revert_to_root = FALSE;
    gboolean skipped = FALSE;
    int ret = 0;

    GET_REPO_OR_FAIL(repo, repo_id);
    GET_COMMIT_OR_FAIL(head_commit, repo->id, repo->version, repo->head->commit_id);

    /* If old_commit_id is head commit, do nothing. */
    if (strcmp(repo->head->commit_id, old_commit_id) == 0) {
        g_debug ("[revert dir] commit is head, do nothing\n");
        goto out;
    }

    if (!old_commit) {
        GET_COMMIT_OR_FAIL(old_commit, repo->id, repo->version, old_commit_id);
        if (strcmp(old_commit->repo_id, repo_id) != 0) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_BAD_COMMIT,
                         "bad commit id");
            ret = -1;
            goto out;
        }
    }

    if (!canon_path) {
        canon_path = get_canonical_path (dir_path);

        parent_dir  = g_path_get_dirname(canon_path);
        dirname = g_path_get_basename(canon_path);

        old_dent = get_dirent_by_path (repo, old_commit->root_id,
                                       parent_dir, dirname, error);
        if (!old_dent || S_ISREG(old_dent->mode)) {
            ret = -1;
            goto out;
        }
        if (*error) {
            syncw_warning ("[revert dir] error: %s\n", (*error)->message);
            g_clear_error (error);
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                         "internal error");
            ret = -1;
            goto out;
        }
    }

    parent_dir_exist = detect_path_exist (repo,
                                          head_commit->root_id,
                                          parent_dir, error);
    if (*error) {
        syncw_warning ("[revert dir] error: %s\n", (*error)->message);
        g_clear_error (error);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "internal error");
        ret = -1;
        goto out;
    }
    
    if (!parent_dir_exist) {
        /* When parent dir does not exist, revert this file to root dir. */
        revert_to_root = TRUE;
        root_id = revert_dir (repo,
                              head_commit->root_id,
                              "/",
                              old_dent,
                              &skipped, error);
    } else {
        revert_to_root = FALSE;
        root_id = revert_dir (repo,
                              head_commit->root_id,
                              parent_dir,
                              old_dent,
                              &skipped, error);
    }

    if (*error) {
        syncw_warning ("[revert dir] error: %s\n", (*error)->message);
        g_clear_error (error);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "internal error");
        ret = -1;
        goto out;
    }

    if (skipped) {
        goto out;
    }
    
    if (!root_id) {
        syncw_warning ("[revert dir] Failed to revert dir.\n");
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to revert dir");
        ret = -1;
        goto out;
    }

    /* Commit. */
    snprintf(buf, SYNCW_PATH_MAX, "Recovered deleted directory \"%s\"", dirname);
    if (gen_new_commit (repo_id, head_commit, root_id,
                        user, buf, NULL, error) < 0) {
        ret = -1;
        goto out;
    }

    syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref (head_commit);
    if (old_commit)
        syncw_commit_unref (old_commit);

    g_free (root_id);
    g_free (parent_dir);
    g_free (dirname);

    g_free (canon_path);
    syncw_dirent_free (old_dent);

#define REVERT_TO_ROOT              0x1
    if (ret == 0) {
        if (revert_to_root)
            ret |= REVERT_TO_ROOT;

        update_repo_size (repo_id);
    }

    return ret;
}

typedef struct CollectRevisionParam CollectRevisionParam;

struct CollectRevisionParam {
    SyncwRepo *repo;
    const char *path;
    GList *wanted_commits;
    GList *file_id_list;
    GList *file_size_list;
    int n_commits;
    GHashTable *file_info_cache;
    
    /* > 0: keep a period of history;
     * == 0: N/A
     * < 0: keep all history data.
     */
    gint64 truncate_time;
    gboolean got_latest;
    gboolean got_second;
    gboolean not_found_file;

    GError **error;
};

typedef struct FileInfo {
    gint64 file_size;
    char *file_id;
    GList *dir_ids;
} FileInfo;

static void
free_file_info (gpointer info)
{
    if (!info)
        return;

    FileInfo *file_info = info;
    g_free (file_info->file_id);
    g_list_free_full (file_info->dir_ids, g_free);
    g_free (file_info);
}

// compare current commit dir_id with pre commit
// if dir_id doesn't change, it means subdir doesn't change, append all sub_dir ids of prev to current
// that is it is no need to traverse all sub dir, if root doesn't change
static gboolean
compare_or_add_id (GList *dir_ids,
                   GList **cur_dir_ids,
                   const char *dir_id)
{
    gboolean ret = FALSE;
    GList *tmp = dir_ids;

    if (tmp == NULL ||
        strcmp ((char *)tmp->data, dir_id) != 0) {
        *cur_dir_ids = g_list_append (*cur_dir_ids, g_strdup (dir_id));
    } else {
        // file doesn't changed, append all dir ids to this commit cache
        while (tmp) {
            *cur_dir_ids = g_list_append (*cur_dir_ids,
                                          g_strdup ((char *)tmp->data));
            tmp = tmp->next;
        }
        ret = TRUE;
    }

    return ret;
}

// dir_ids: all dir_ids in prev commit, in the order of fs tree
// cur_dir_ids: all dir_ids in current commit
// if no error and returned syncwdir is NULL, then it means
// searched dir doesn't change in pre and current commit
static SyncwDir*
get_syncwdir_by_path (const char *repo_id,
                     int version,
                     const char *root_id,
                     const char *path,
                     GList *dir_ids,
                     GList **cur_dir_ids,
                     GError **error)
{
    SyncwDir *dir = NULL;
    SyncwDirent *dent;
    const char *dir_id = root_id;
    char *name, *saveptr;
    char *tmp_path = NULL;
    GList *tmp = dir_ids;

    dir = syncw_fs_manager_get_syncwdir (syncw->fs_mgr, repo_id, version, dir_id);
    if (!dir) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_DIR_MISSING, "directory is missing");
        goto out;
    }

    if (compare_or_add_id (tmp, cur_dir_ids, dir_id)) {
        syncw_dir_free (dir);
        dir = NULL;
        goto out;
    } else if (tmp) {
        tmp = tmp->next;
    }

    if (strcmp (path, ".") == 0 ||
        strcmp (path, "/") == 0) {
        goto out;
    } else {
        tmp_path = g_strdup (path);
    }

    name = strtok_r (tmp_path, "/", &saveptr);
    while (name != NULL) {
        GList *l;
        for (l = dir->entries; l != NULL; l = l->next) {
            dent = l->data;

            if (strcmp(dent->name, name) == 0 && S_ISDIR(dent->mode)) {
                dir_id = dent->id;
                break;
            }
        }

        if (!l) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_PATH_NO_EXIST,
                         "Path does not exists %s", path);
            syncw_dir_free (dir);
            dir = NULL;
            break;
        }

        if (compare_or_add_id (tmp, cur_dir_ids, dir_id)) {
            syncw_dir_free (dir);
            dir = NULL;
            goto out;
        } else if (tmp) {
            tmp = tmp->next;
        }

        SyncwDir *prev = dir;
        dir = syncw_fs_manager_get_syncwdir (syncw->fs_mgr, repo_id, version, dir_id);
        syncw_dir_free (prev);

        if (!dir) {
            g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_DIR_MISSING,
                         "directory is missing");
            break;
        }

        name = strtok_r (NULL, "/", &saveptr);
    }

out:
    g_free (tmp_path);
    return dir;
}

/*
 * Return NULL if file is not found, error is still NULL;
 * If we have IO errors, error is set.
 */
static FileInfo*
get_file_info (SyncwRepo *repo,
               SyncwCommit *commit,
               const char *path,
               GHashTable *file_info_cache,
               FileInfo *last_info,
               GError **error)
{
    SyncwDir *dir = NULL;
    SyncwDirent *dirent = NULL;
    FileInfo *file_info = NULL;
    GList *tmp;

    file_info = g_hash_table_lookup (file_info_cache, commit->commit_id);
    if (file_info)
        return file_info;

    char *dir_name = g_path_get_dirname (path);
    char *file_name = g_path_get_basename (path);
    GList *cur_dir_ids = NULL;
    GList *dir_ids = last_info ? last_info->dir_ids : NULL;

    dir = get_syncwdir_by_path (repo->store_id, repo->version,
                               commit->root_id, dir_name, dir_ids,
                               &cur_dir_ids, error);
    if (*error) {
        if ((*error)->code == SYNCW_ERR_PATH_NO_EXIST)
            g_clear_error (error);
        goto out;
    }

    if (!dir) {
        // if no error and return is null from get_syncwdir_by_path, it means dir doesn't
        // change in pre and current commit, so the last_info (file info of pre commit)
        // is also the current file info
        file_info = g_new0 (FileInfo, 1);
        file_info->file_id = g_strdup (last_info->file_id);
        file_info->dir_ids = cur_dir_ids;
        file_info->file_size = last_info->file_size;
        g_hash_table_insert (file_info_cache, g_strdup (commit->commit_id),
                             file_info);
    } else {
        for (tmp = dir->entries; tmp; tmp = tmp->next) {
            dirent = tmp->data;
            if (strcmp (file_name, dirent->name) == 0 &&
                S_ISREG (dirent->mode)) {
                break;
            }
        }
        if (tmp) {
            // from parent dir find the file, cache file info for the next compare
            file_info = g_new0 (FileInfo, 1);
            file_info->file_id = g_strdup (dirent->id);
            file_info->dir_ids = cur_dir_ids;
            if (repo->version > 0) {
                file_info->file_size = dirent->size;
            } else {
                file_info->file_size = syncw_fs_manager_get_file_size (syncw->fs_mgr,
                                                                      repo->store_id,
                                                                      repo->version,
                                                                      dirent->id);
            }
            g_hash_table_insert (file_info_cache, g_strdup (commit->commit_id),
                                 file_info);
        }
    }

out:
    if (dir)
        syncw_dir_free (dir);
    if (!file_info) {
        g_list_free_full (cur_dir_ids, g_free);
    }
    g_free (file_name);
    g_free (dir_name);

    return file_info;
}

static void
add_revision_info (CollectRevisionParam *data,
                   SyncwCommit *commit, const char *file_id, gint64 file_size)
{
    syncw_commit_ref (commit);
    data->wanted_commits = g_list_prepend (data->wanted_commits, commit);
    data->file_id_list = g_list_prepend (data->file_id_list, g_strdup(file_id));
    gint64 *size = g_malloc(sizeof(gint64));
    *size = file_size;
    data->file_size_list = g_list_prepend (data->file_size_list, size);
    ++(data->n_commits);
}

static gboolean
collect_file_revisions (SyncwCommit *commit, void *vdata, gboolean *stop)
{
    CollectRevisionParam *data = vdata;
    SyncwRepo *repo = data->repo;
    const char *path = data->path;
    GError **error = data->error;
    GHashTable *file_info_cache = data->file_info_cache;
    FileInfo *file_info = NULL;
    FileInfo *parent1_info = NULL;
    FileInfo *parent2_info = NULL;

    SyncwCommit *parent_commit = NULL;
    SyncwCommit *parent_commit2 = NULL;

    gboolean ret = TRUE;

    /* At least find the latest revision. */
    if (data->got_latest && data->truncate_time == 0) {
        *stop = TRUE;
        return TRUE;
    }

    if (data->got_latest &&
        data->truncate_time > 0 &&
        (gint64)(commit->ctime) < data->truncate_time &&
        data->got_second)
    {
        *stop = TRUE;
        data->not_found_file = TRUE;
        return TRUE;
    }

    g_clear_error (error);

    file_info = get_file_info (data->repo, commit, path,
                               file_info_cache, NULL, error);
    if (*error) {
        syncw_warning ("Error when finding %s under %s:%s\n",
                      path, data->repo->id, commit->commit_id);
        ret = FALSE;
        goto out;
    }

    if (!file_info) {
        /* Target file is not present in this commit.
         * Stop traversing after finding the initial version.
         * Deleted files with the same path are not included in history.
         */
        *stop = TRUE;
        data->not_found_file = TRUE;
        goto out;
    }

    if (!commit->parent_id) {
        /* Initial commit */
        add_revision_info (data, commit, file_info->file_id, file_info->file_size);
        goto out;
    }

    parent_commit = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                                    repo->id, repo->version,
                                                    commit->parent_id);
    if (!parent_commit) {
        syncw_warning ("Failed to get commit %s:%s\n", repo->id, commit->parent_id);
        ret = FALSE;
        goto out;
    }

    parent1_info = get_file_info (data->repo, parent_commit, path,
                                  file_info_cache, file_info, error);
    if (*error) {
        syncw_warning ("Error when finding %s under %s:%s\n",
                      path, data->repo->id, parent_commit->commit_id);
        ret = FALSE;
        goto out;
    }

    if (parent1_info &&
        g_strcmp0 (parent1_info->file_id, file_info->file_id) == 0) {
        /* This commit does not modify the target file */
        goto out;
    }

    /* In case of a merge, the second parent also need compare */
    if (commit->second_parent_id) {
        parent_commit2 = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                                         repo->id, repo->version,
                                                         commit->second_parent_id);
        if (!parent_commit2) {
            syncw_warning ("Failed to get commit %s:%s\n",
                          repo->id, commit->second_parent_id);
            ret = FALSE;
            goto out;
        }

        parent2_info = get_file_info (data->repo, parent_commit2, path,
                                      file_info_cache, file_info, error);
        if (*error) {
            syncw_warning ("Error when finding %s under %s:%s\n",
                          path, data->repo->id, parent_commit2->commit_id);
            ret = FALSE;
            goto out;
        }

        if (parent2_info &&
            g_strcmp0 (parent2_info->file_id, file_info->file_id) == 0) {
            /* This commit does not modify the target file */
            goto out;
        }
    }

    if (!data->got_latest) {
        data->got_latest = TRUE;
    } else {
        if (!data->got_second)
            data->got_second = TRUE;
    }
    add_revision_info (data, commit, file_info->file_id, file_info->file_size);

out:
    if (parent_commit) syncw_commit_unref (parent_commit);
    if (parent_commit2) syncw_commit_unref (parent_commit2);

    g_hash_table_remove (file_info_cache, commit->commit_id);

    return ret;
}

static gboolean
path_exists_in_commit (SyncwRepo *repo, const char *commit_id, const char *path)
{
    SyncwCommit *c = NULL;
    char *obj_id;
    guint32 mode;

    c = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                        repo->id, repo->version,
                                        commit_id);
    if (!c) {
        syncw_warning ("Failed to get commit %s:%.8s.\n", repo->id, commit_id);
        return FALSE;
    }
    obj_id = syncw_fs_manager_path_to_obj_id (syncw->fs_mgr,
                                             repo->store_id,
                                             repo->version,
                                             c->root_id,
                                             path,
                                             &mode,
                                             NULL);
    syncw_commit_unref (c);
    if (!obj_id)
        return FALSE;
    g_free (obj_id);
    return TRUE;
}

static gboolean
detect_rename_revision (SyncwRepo *repo,
                        SyncwCommit *commit,
                        const char *path,
                        char **parent_id,
                        char **old_path)
{
    GList *diff_res = NULL;
    SyncwCommit *p1 = NULL;
    int rc;
    gboolean is_renamed = FALSE;

    while (*path == '/' && *path != 0)
        ++path;

    if (!commit->second_parent_id) {
        p1 = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                             repo->id, repo->version,
                                             commit->parent_id);
        if (!p1) {
            syncw_warning ("Failed to get commit %s:%.8s.\n",
                          repo->id, commit->parent_id);
            return FALSE;
        }
        /* Don't fold diff results for directories. We need to know a file was
         * renamed when its parent folder was renamed.
         */
        rc = diff_commits (p1, commit, &diff_res, FALSE);
        syncw_commit_unref (p1);
        if (rc < 0) {
            syncw_warning ("Failed to diff.\n");
            return FALSE;
        }
    } else {
        rc = diff_merge (commit, &diff_res, FALSE);
        if (rc < 0) {
            syncw_warning ("Failed to diff merge.\n");
            return FALSE;
        }
    }

    GList *ptr;
    DiffEntry *de;
    for (ptr = diff_res; ptr; ptr = ptr->next) {
        de = ptr->data;
        if (de->status == DIFF_STATUS_RENAMED && strcmp (de->new_name, path) == 0) {
            *old_path = g_strdup(de->name);
            is_renamed = TRUE;
            break;
        }
    }
    for (ptr = diff_res; ptr; ptr = ptr->next)
        diff_entry_free ((DiffEntry *)ptr->data);
    g_list_free (diff_res);

    if (!is_renamed)
        return FALSE;

    /* Determine parent commit containing the old path. */
    if (!commit->second_parent_id)
        *parent_id = g_strdup(commit->parent_id);
    else {
        if (path_exists_in_commit (repo, commit->parent_id, *old_path))
            *parent_id = g_strdup(commit->parent_id);
        else if (path_exists_in_commit (repo, commit->second_parent_id, *old_path))
            *parent_id = g_strdup(commit->second_parent_id);
        else {
            g_free (*old_path);
            *old_path = NULL;
            return FALSE;
        }
    }

    return TRUE;
}

static SyncwerkCommit *
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

static GList *
convert_rpc_commit_list (GList *commit_list,
                         GList *file_id_list,
                         GList *file_size_list,
                         gboolean is_renamed,
                         const char *renamed_old_path)
{
    GList *ret = NULL;
    GList *ptr1, *ptr2, *ptr3;
    SyncwCommit *c;
    char *file_id;
    gint64 *file_size;
    SyncwerkCommit *commit;

    for (ptr1 = commit_list, ptr2 = file_id_list, ptr3 = file_size_list;
         ptr1 && ptr2 && ptr3;
         ptr1 = ptr1->next, ptr2 = ptr2->next, ptr3 = ptr3->next) {
        c = ptr1->data;
        file_id = ptr2->data;
        file_size = ptr3->data;
        commit = convert_to_syncwerk_commit (c);
        g_object_set (commit, "rev_file_id", file_id, "rev_file_size", *file_size,
                      NULL);
        if (ptr1->next == NULL && is_renamed)
            g_object_set (commit, "rev_renamed_old_path", renamed_old_path, NULL);
        ret = g_list_prepend (ret, commit);
    }

    ret = g_list_reverse (ret);
    return ret;
}

GList *
syncw_repo_manager_list_file_revisions (SyncwRepoManager *mgr,
                                       const char *repo_id,
                                       const char *start_commit_id,
                                       const char *path,
                                       int limit,
                                       gboolean got_latest,
                                       gboolean got_second,
                                       GError **error)
{
    SyncwRepo *repo = NULL;
    GList *commit_list = NULL, *file_id_list = NULL, *file_size_list = NULL;
    GList *ret = NULL, *ptr;
    CollectRevisionParam data = {0};
    SyncwCommit *last_commit = NULL;
    const char *head_id;
    gboolean is_renamed = FALSE;
    char *parent_id = NULL, *old_path = NULL;
    char *next_start_commit= NULL;

    repo = syncw_repo_manager_get_repo (mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "No such repo %s", repo_id);
        goto out;
    }

    data.repo = repo;

    if (!start_commit_id)
        head_id = repo->head->commit_id;
    else
        head_id = start_commit_id;

    data.path = path;
    data.error = error;

    data.truncate_time = syncw_repo_manager_get_repo_truncate_time (mgr, repo_id);

    data.wanted_commits = NULL;
    data.file_id_list = NULL;
    data.file_size_list = NULL;
    data.got_latest = got_latest;
    data.got_second = got_second;
    data.not_found_file = FALSE;

    /* A hash table to cache caculated file info of <path> in <commit> */
    data.file_info_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, free_file_info);

    if (!syncw_commit_manager_traverse_commit_tree_with_limit (syncw->commit_mgr,
                                                              repo->id,
                                                              repo->version,
                                                              head_id,
                                                              (CommitTraverseFunc)collect_file_revisions,
                                                              limit, &data, &next_start_commit, TRUE)) {
        g_clear_error (error);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "failed to traverse commit of repo %s", repo_id);
        goto out;
    }

    if (data.wanted_commits) {
        last_commit = data.wanted_commits->data;
        is_renamed = detect_rename_revision (repo,
                                             last_commit, path, &parent_id, &old_path);
        if (data.not_found_file && !is_renamed) {   // reached file initial commit.
            g_free (next_start_commit);
            next_start_commit = NULL;
        } else if (is_renamed){    // file renamed.
            g_free (next_start_commit);
            next_start_commit = g_strdup (parent_id);
        }
        commit_list = g_list_reverse (data.wanted_commits);
        file_id_list = g_list_reverse (data.file_id_list);
        file_size_list = g_list_reverse (data.file_size_list);

        char *rename_path = NULL;
        if (old_path && *old_path != '/')
            rename_path = g_strconcat ("/", old_path, NULL);
        else
            rename_path = g_strdup (old_path);

        ret = convert_rpc_commit_list (commit_list, file_id_list, file_size_list,
                                       is_renamed, rename_path);
        g_free (rename_path);
    } else {
        if (data.not_found_file) {
            g_free (next_start_commit);
            next_start_commit = NULL;
        }
    }

    /* Append one commit that only contains 'next_start_commit' */
    SyncwerkCommit *commit = syncwerk_commit_new ();
    g_object_set (commit, "next_start_commit", next_start_commit, NULL);
    ret = g_list_append (ret, commit);

out:
    if (repo)
        syncw_repo_unref (repo);
    for (ptr = commit_list; ptr; ptr = ptr->next)
        syncw_commit_unref ((SyncwCommit *)ptr->data);
    g_list_free (commit_list);
    string_list_free (file_id_list);
    for (ptr = file_size_list; ptr; ptr = ptr->next)
        g_free (ptr->data);
    g_list_free (file_size_list);
    if (data.file_info_cache)
        g_hash_table_destroy (data.file_info_cache);
    g_free (old_path);
    g_free (parent_id);
    g_free (next_start_commit);

    return ret;
}

typedef struct CalcFilesLastModifiedParam CalcFilesLastModifiedParam;

struct CalcFilesLastModifiedParam {
    SyncwRepo *repo;
    GError **error;
    const char *parent_dir;
    GHashTable *last_modified_hash;
    GHashTable *current_file_id_hash;
    SyncwCommit *current_commit;
};

static gboolean
check_non_existing_files (void *key, void *value, void *vdata)
{
    CalcFilesLastModifiedParam *data = vdata;
    gboolean remove = FALSE;
    
    char *file_name = key;
    gint64 *ctime = g_hash_table_lookup (data->last_modified_hash, file_name);
    if (!ctime) {
        /* Impossible */
        remove = TRUE;
    } else if (*ctime != data->current_commit->ctime) {
        /* This file does not exist in this commit. So it's last modified in
         * the previous commit.
         */
        remove = TRUE;
    }

    return remove;
}

static gboolean
collect_files_last_modified (SyncwCommit *commit, void *vdata, gboolean *stop)
{
    CalcFilesLastModifiedParam *data = vdata;
    GError **error = data->error;
    SyncwDirent *dent = NULL;
    char *file_id = NULL;
    SyncwDir *dir = NULL;
    GList *ptr;
    gboolean ret = TRUE;

    data->current_commit = commit;
    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               data->repo->store_id,
                                               data->repo->version,
                                               commit->root_id,
                                               data->parent_dir,
                                               error);
    if (*error) {
        if (!g_error_matches(*error, SYNCWERK_DOMAIN, SYNCW_ERR_PATH_NO_EXIST)) {
            *stop = TRUE;
            ret = FALSE;
            goto out;
        } else {
            g_clear_error (error);
        }
    }

    if (!dir) {
        /* The directory does not exist in this commit. So all files are last
         * modified in the previous commit;
         */
        *stop = TRUE;
        goto out;
    }

    for (ptr = dir->entries; ptr; ptr = ptr->next) {
        dent = ptr->data;
        file_id = g_hash_table_lookup (data->current_file_id_hash, dent->name);
        if (file_id) {
            if (strcmp(file_id, dent->id) != 0) {
                g_hash_table_remove (data->current_file_id_hash, dent->name);
            } else {
                gint64 *ctime = g_new (gint64, 1);
                *ctime = commit->ctime;
                g_hash_table_replace (data->last_modified_hash, g_strdup(dent->name), ctime);
            }
        }

        if (g_hash_table_size(data->current_file_id_hash) == 0) {
            *stop = TRUE;
            goto out;
        }
    }

    /* Files not found in the current commit are last modified in the previous
     * commit */
    g_hash_table_foreach_remove (data->current_file_id_hash,
                                 check_non_existing_files, data);

    if (g_hash_table_size(data->current_file_id_hash) == 0) {
        /* All files under this diretory have been calculated  */
        *stop = TRUE;
        goto out;
    }

out:
    syncw_dir_free (dir);

    return ret;
}

/**
 * Give a directory, return the last modification timestamps of all the files
 * under this directory.
 *
 * First we record the current id of every file, then traverse the commit
 * tree. Give a commit, for each file, if the file id in that commit is
 * different than its current id, then this file is last modified in the
 * commit previous to that commit.
 */
GList *
syncw_repo_manager_calc_files_last_modified (SyncwRepoManager *mgr,
                                            const char *repo_id,
                                            const char *parent_dir,
                                            int limit,
                                            GError **error)
{
    SyncwRepo *repo = NULL;
    SyncwCommit *head_commit = NULL;
    SyncwDir *dir = NULL;
    GList *ptr = NULL;
    SyncwDirent *dent = NULL; 
    CalcFilesLastModifiedParam data = {0};
    GList *ret_list = NULL;

    repo = syncw_repo_manager_get_repo (mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "No such repo %s", repo_id);
        goto out;
    }

    head_commit = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                                  repo->id, repo->version, 
                                                  repo->head->commit_id);
    if (!head_commit) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Failed to get commit %s", repo->head->commit_id);
        goto out;
    }

    dir = syncw_fs_manager_get_syncwdir_by_path (syncw->fs_mgr,
                                               repo->store_id, repo->version,
                                               head_commit->root_id,
                                               parent_dir, error);
    if (*error || !dir) {
        goto out;
    }

    data.repo = repo;
    
    /* A hash table of pattern (file_name, current_file_id) */
    data.current_file_id_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                       g_free, g_free);
    /* A (file_name, last_modified) hashtable. <last_modified> is a heap
       allocated gint64
    */
    data.last_modified_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                     g_free, g_free);
    for (ptr = dir->entries; ptr; ptr = ptr->next) {
        dent = ptr->data;
        g_hash_table_insert (data.current_file_id_hash,
                             g_strdup(dent->name),
                             g_strdup(dent->id));

        gint64 *ctime = g_new (gint64, 1);
        *ctime = head_commit->ctime;
        g_hash_table_insert (data.last_modified_hash,
                             g_strdup(dent->name), 
                             ctime);
    }

    if (g_hash_table_size (data.current_file_id_hash) == 0) {
        /* An empty directory, no need to traverse */
        goto out;
    }

    data.parent_dir = parent_dir;
    data.error = error;

    if (!syncw_commit_manager_traverse_commit_tree_with_limit (syncw->commit_mgr,
                                                              repo->id, repo->version, 
                                                        repo->head->commit_id,
                                (CommitTraverseFunc)collect_files_last_modified,
                                                              limit, &data, NULL, FALSE)) {
        if (*error)
            syncw_warning ("error when traversing commits: %s\n", (*error)->message);
        else
            syncw_warning ("error when traversing commits.\n");
        g_clear_error (error);
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "failed to traverse commit of repo %s", repo_id);
        goto out;
    }

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, data.last_modified_hash);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        SyncwerkFileLastModifiedInfo *info;
        gint64 last_modified = *(gint64 *)value;
        info = g_object_new (SYNCWERK_TYPE_FILE_LAST_MODIFIED_INFO,
                             "file_name", key,
                             "last_modified", last_modified,
                             NULL);
        ret_list = g_list_prepend (ret_list, info);
    }

out:
    if (repo)
        syncw_repo_unref (repo);
    if (head_commit)
        syncw_commit_unref(head_commit);
    if (data.last_modified_hash)
        g_hash_table_destroy (data.last_modified_hash);
    if (data.current_file_id_hash)
        g_hash_table_destroy (data.current_file_id_hash);
    if (dir)
        syncw_dir_free (dir);

    return g_list_reverse(ret_list);
}

int
syncw_repo_manager_revert_on_server (SyncwRepoManager *mgr,
                                    const char *repo_id,
                                    const char *commit_id,
                                    const char *user_name,
                                    GError **error)
{
    SyncwRepo *repo;
    SyncwCommit *commit = NULL, *new_commit = NULL;
    char desc[512];
    int ret = 0;

retry:
    repo = syncw_repo_manager_get_repo (mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "No such repo");
        return -1;
    }

    commit = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                             repo->id, repo->version, 
                                             commit_id);
    if (!commit) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Commit doesn't exist");
        ret = -1;
        goto out;
    }

#ifndef WIN32
    strftime (desc, sizeof(desc), "Reverted repo to status at %F %T.", 
              localtime((time_t *)(&commit->ctime)));
#else
    strftime (desc, sizeof(desc), "Reverted repo to status at %Y-%m-%d %H:%M:%S.",
              localtime((time_t *)(&commit->ctime)));
#endif

    new_commit = syncw_commit_new (NULL, repo->id, commit->root_id,
                                  user_name, EMPTY_SHA1,
                                  desc, 0);

    new_commit->parent_id = g_strdup (repo->head->commit_id);
    syncw_repo_to_commit (repo, new_commit);

    if (syncw_commit_manager_add_commit (syncw->commit_mgr, new_commit) < 0) {
        ret = -1;
        goto out;
    }

    syncw_branch_set_commit (repo->head, new_commit->commit_id);
    if (syncw_branch_manager_test_and_update_branch (syncw->branch_mgr,
                                                    repo->head,
                                                    new_commit->parent_id) < 0)
    {
        syncw_repo_unref (repo);
        syncw_commit_unref (commit);
        syncw_commit_unref (new_commit);
        repo = NULL;
        commit = new_commit = NULL;
        goto retry;
    }

    syncw_repo_manager_merge_virtual_repo (mgr, repo_id, NULL);

out:
    if (new_commit)
        syncw_commit_unref (new_commit);
    if (commit)
        syncw_commit_unref (commit);
    if (repo)
        syncw_repo_unref (repo);

    if (ret == 0) {
        update_repo_size (repo_id);
    }

    return ret;
}

static void
add_deleted_entry (SyncwRepo *repo,
                   GHashTable *entries,
                   SyncwDirent *dent,
                   const char *base,
                   SyncwCommit *child,
                   SyncwCommit *parent)
{
    char *path = g_strconcat (base, dent->name, NULL);
    SyncwerkDeletedEntry *entry;
    Syncwerk *file;

    if (g_hash_table_lookup (entries, path) != NULL) {
        /* g_debug ("found dup deleted entry for %s.\n", path); */
        g_free (path);
        return;
    }

    /* g_debug ("Add deleted entry for %s.\n", path); */

    entry = g_object_new (SYNCWERK_TYPE_DELETED_ENTRY,
                          "commit_id", parent->commit_id,
                          "obj_id", dent->id,
                          "obj_name", dent->name,
                          "basedir", base,
                          "mode", dent->mode,
                          "delete_time", child->ctime,
                          NULL);

    if (S_ISREG(dent->mode)) {
        file = syncw_fs_manager_get_syncwerk (syncw->fs_mgr,
                                            repo->store_id, repo->version,
                                            dent->id);
        if (!file) {
            g_free (path);
            g_object_unref (entry);
            return;
        }
        g_object_set (entry, "file_size", file->file_size, NULL);
        syncwerk_unref (file);
    }

    g_hash_table_insert (entries, path, entry);
}

static int
find_deleted_recursive (SyncwRepo *repo,
                        SyncwDir *d1,
                        SyncwDir *d2,
                        const char *base,
                        SyncwCommit *child,
                        SyncwCommit *parent,
                        GHashTable *entries)
{
    GList *p1, *p2;
    SyncwDirent *dent1, *dent2;
    int res, ret = 0;

    p1 = d1->entries;
    p2 = d2->entries;

    /* Since dirents are sorted in descending order, we can use merge
     * algorithm to find out deleted entries.
     * Deleted entries are those:
     * 1. exists in d2 but absent in d1.
     * 2. exists in both d1 and d2 but with different type.
     */

    while (p1 && p2) {
        dent1 = p1->data;
        dent2 = p2->data;

        res = g_strcmp0 (dent1->name, dent2->name);
        if (res < 0) {
            /* exists in d2 but absent in d1. */
            add_deleted_entry (repo, entries, dent2, base, child, parent);
            p2 = p2->next;
        } else if (res == 0) {
            if ((dent1->mode & S_IFMT) != (dent2->mode & S_IFMT)) {
                /* both exists but with diffent type. */
                add_deleted_entry (repo, entries, dent2, base, child, parent);
            } else if (S_ISDIR(dent1->mode) && strcmp(dent1->id, dent2->id) != 0) {
                SyncwDir *n1 = syncw_fs_manager_get_syncwdir_sorted (syncw->fs_mgr,
                                                                  repo->id,
                                                                  repo->version,
                                                                  dent1->id);
                if (!n1) {
                    syncw_warning ("Failed to find dir %s:%s.\n", repo->id, dent1->id);
                    return -1;
                }

                SyncwDir *n2 = syncw_fs_manager_get_syncwdir_sorted (syncw->fs_mgr,
                                                                  repo->id,
                                                                  repo->version,
                                                                  dent2->id);
                if (!n2) {
                    syncw_warning ("Failed to find dir %s:%s.\n", repo->id, dent2->id);
                    syncw_dir_free (n1);
                    return -1;
                }

                char *new_base = g_strconcat (base, dent1->name, "/", NULL);
                ret = find_deleted_recursive (repo, n1, n2, new_base,
                                              child, parent, entries);
                g_free (new_base);
                syncw_dir_free (n1);
                syncw_dir_free (n2);
                if (ret < 0)
                    return ret;
            }
            p1 = p1->next;
            p2 = p2->next;
        } else {
            p1 = p1->next;
        }
    }

    for ( ; p2 != NULL; p2 = p2->next) {
        dent2 = p2->data;
        add_deleted_entry (repo, entries, dent2, base, child, parent);
    }

    return ret;
}

static int
find_deleted (SyncwRepo *repo,
              SyncwCommit *child,
              SyncwCommit *parent,
              const char *base,
              GHashTable *entries)
{
    SyncwDir *d1, *d2;
    int ret = 0;

    d1 = syncw_fs_manager_get_syncwdir_sorted_by_path (syncw->fs_mgr,
                                                     repo->id,
                                                     repo->version,
                                                     child->root_id, base);
    if (!d1) {
        return ret;
    }

    d2 = syncw_fs_manager_get_syncwdir_sorted_by_path (syncw->fs_mgr,
                                                     repo->id,
                                                     repo->version,
                                                     parent->root_id, base);
    if (!d2) {
        syncw_dir_free (d1);
        return ret;
    }

    ret = find_deleted_recursive (repo, d1, d2, base, child, parent, entries);

    syncw_dir_free (d2);
    syncw_dir_free (d1);

    return ret;
}

typedef struct CollectDelData {
    SyncwRepo *repo;
    GHashTable *entries;
    gint64 truncate_time;
    char *path;
} CollectDelData;

#define DEFAULT_RECYCLE_DAYS 7

static gboolean
collect_deleted (SyncwCommit *commit, void *vdata, gboolean *stop)
{
    CollectDelData *data = vdata;
    SyncwRepo *repo = data->repo;
    GHashTable *entries = data->entries;
    gint64 truncate_time = data->truncate_time;
    SyncwCommit *p1, *p2;

    /* We use <= here. This is for handling clean trash and history.
     * If the user cleans all history, truncate time will be equal to
     * the head commit's ctime. In such case, we don't actually want to display
     * any deleted file.
     */
    if ((gint64)(commit->ctime) <= truncate_time) {
        *stop = TRUE;
        return TRUE;
    }

    if (commit->parent_id == NULL)
        return TRUE;

    if (!(strstr (commit->desc, PREFIX_DEL_FILE) != NULL ||
          strstr (commit->desc, PREFIX_DEL_DIR) != NULL ||
          strstr (commit->desc, PREFIX_DEL_DIRS) != NULL)) {
        return TRUE;
    }

    p1 = syncw_commit_manager_get_commit (commit->manager,
                                         repo->id, repo->version,
                                         commit->parent_id);
    if (!p1) {
        syncw_warning ("Failed to find commit %s:%s.\n", repo->id, commit->parent_id);
        return FALSE;
    }

    if (find_deleted (data->repo, commit, p1, data->path, entries) < 0) {
        syncw_commit_unref (p1);
        return FALSE;
    }

    syncw_commit_unref (p1);

    if (commit->second_parent_id) {
        p2 = syncw_commit_manager_get_commit (commit->manager,
                                             repo->id, repo->version,
                                             commit->second_parent_id);
        if (!p2) {
            syncw_warning ("Failed to find commit %s:%s.\n",
                          repo->id, commit->second_parent_id);
            return FALSE;
        }

        if (find_deleted (data->repo, commit, p2, data->path, entries) < 0) {
            syncw_commit_unref (p2);
            return FALSE;
        }

        syncw_commit_unref (p2);
    }

    return TRUE;
}

typedef struct RemoveExistingParam {
    SyncwRepo *repo;
    SyncwCommit *head;
} RemoveExistingParam;

static gboolean
remove_existing (gpointer key, gpointer value, gpointer user_data)
{
    SyncwerkDeletedEntry *e = value;
    RemoveExistingParam *param = user_data;
    SyncwRepo *repo = param->repo;
    SyncwCommit *head = param->head;
    guint32 mode = syncwerk_deleted_entry_get_mode(e), mode_out = 0;
    char *path = key;

    char *obj_id = syncw_fs_manager_path_to_obj_id (syncw->fs_mgr,
                                                   repo->store_id, repo->version,
                                                   head->root_id,
                                                   path, &mode_out, NULL);
    if (obj_id == NULL)
        return FALSE;
    g_free (obj_id);

    /* If path exist in head commit and with the same type,
     * remove it from deleted entries.
     */
    if ((mode & S_IFMT) == (mode_out & S_IFMT)) {
        /* g_debug ("%s exists in head commit.\n", path); */
        return TRUE;
    }

    return FALSE;
}

static int
filter_out_existing_entries (GHashTable *entries,
                             SyncwRepo *repo,
                             const char *head_id)
{
    SyncwCommit *head;
    RemoveExistingParam param;

    head = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                           repo->id, repo->version, 
                                           head_id);
    if (!head) {
        syncw_warning ("Failed to find head commit %s of repo %s.\n",
                      head_id, repo->id);
        return -1;
    }

    param.repo = repo;
    param.head = head;

    g_hash_table_foreach_remove (entries, remove_existing, &param);

    syncw_commit_unref (head);
    return 0;
}

static gboolean
hash_to_list (gpointer key, gpointer value, gpointer user_data)
{
    GList **plist = (GList **)user_data;

    g_free (key);
    *plist = g_list_prepend (*plist, value);

    return TRUE;
}

static gint
compare_commit_by_time (gconstpointer a, gconstpointer b, gpointer unused)
{
    const SyncwCommit *commit_a = a;
    const SyncwCommit *commit_b = b;

    /* Latest commit comes first in the list. */
    return (commit_b->ctime - commit_a->ctime);
}

static int
insert_parent_commit (GList **list, GHashTable *hash,
                      const char *repo_id, int version,
                      const char *parent_id)
{
    SyncwCommit *p;
    char *key;

    if (g_hash_table_lookup (hash, parent_id) != NULL)
        return 0;

    p = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                        repo_id, version,
                                        parent_id);
    if (!p) {
        syncw_warning ("Failed to find commit %s\n", parent_id);
        return -1;
    }

    *list = g_list_insert_sorted_with_data (*list, p,
                                           compare_commit_by_time,
                                           NULL);

    key = g_strdup (parent_id);
    g_hash_table_replace (hash, key, key);

    return 0;
}

static int
scan_commits_for_collect_deleted (CollectDelData *data,
                                  const char *prev_scan_stat,
                                  int limit,
                                  char **next_scan_stat)
{
    GList *list = NULL;
    SyncwCommit *commit;
    GHashTable *commit_hash;
    SyncwRepo *repo = data->repo;
    int scan_num = 0;
    gboolean ret = TRUE;

    /* A hash table for recording id of traversed commits. */
    commit_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    char *key;
    if (prev_scan_stat == NULL) {
        commit = syncw_commit_manager_get_commit (syncw->commit_mgr, repo->id,
                                                 repo->version, repo->head->commit_id);
        if (!commit) {
            ret = FALSE;
            goto out;
        }
        list = g_list_prepend (list, commit);
        key = g_strdup (commit->commit_id);
        g_hash_table_replace (commit_hash, key, key);
    } else {
        commit = syncw_commit_manager_get_commit (syncw->commit_mgr, repo->id,
                                                 repo->version, prev_scan_stat);
        if (!commit) {
            ret = FALSE;
            goto out;
        }
        list = g_list_append (list, commit);
        key = g_strdup (commit->commit_id);
        g_hash_table_replace (commit_hash, key, key);
    }

    while (list) {
        gboolean stop = FALSE;
        commit = list->data;
        list = g_list_delete_link (list, list);

        if (!collect_deleted (commit, data, &stop)) {
            syncw_warning("[comit-mgr] CommitTraverseFunc failed\n");
            syncw_commit_unref (commit);
            ret = FALSE;
            goto out;
        }

        if (stop) {
            syncw_commit_unref (commit);
            /* stop traverse down from this commit,
             * but not stop traversing the tree
             */
            continue;
        }

        if (commit->parent_id) {
            if (insert_parent_commit (&list, commit_hash, repo->id,
                                      repo->version,
                                      commit->parent_id) < 0) {
                syncw_warning("[comit-mgr] insert parent commit failed\n");
                syncw_commit_unref (commit);
                ret = FALSE;
                goto out;
            }
        }
        if (commit->second_parent_id) {
            if (insert_parent_commit (&list, commit_hash, repo->id,
                                      repo->version,
                                      commit->second_parent_id) < 0) {
                syncw_warning("[comit-mgr]insert second parent commit failed\n");
                syncw_commit_unref (commit);
                ret = FALSE;
                goto out;
            }
        }
        syncw_commit_unref (commit);

        if (++scan_num >= limit && (!list || !list->next)) {
            break;
        }
    }

    // two scenarios:
    // 1. list is empty, indicate scan end
    // 2. list only have one commit, as start for next scan
    if (list) {
        commit = list->data;
        *next_scan_stat = g_strdup (commit->commit_id);
        syncw_commit_unref (commit);
        list = g_list_delete_link (list, list);
    }
    g_hash_table_destroy (commit_hash);

    return ret;

out:
    g_hash_table_destroy (commit_hash);
    while (list) {
        commit = list->data;
        syncw_commit_unref (commit);
        list = g_list_delete_link (list, list);
    }

    return ret;
}

GList *
syncw_repo_manager_get_deleted_entries (SyncwRepoManager *mgr,
                                       const char *repo_id,
                                       int show_days,
                                       const char *path,
                                       const char *scan_stat,
                                       int limit,
                                       GError **error)
{
    SyncwRepo *repo;
    gint64 truncate_time, show_time;
    GList *ret = NULL;
    char *next_scan_stat = NULL;

    truncate_time = syncw_repo_manager_get_repo_truncate_time (mgr, repo_id);
    if (truncate_time == 0) {
        // Don't keep history, set scan_stat as NULL, indicate no need for next scan
        ret = g_list_append (ret, g_object_new (SYNCWERK_TYPE_DELETED_ENTRY,
                                                "scan_stat", NULL,
                                                NULL));
        return ret;
    }

    if (show_days <= 0)
        show_time = -1;
    else
        show_time = (gint64)time(NULL) - show_days * 24 * 3600;

    repo = syncw_repo_manager_get_repo (mgr, repo_id);
    if (!repo) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_GENERAL,
                     "Invalid repo id");
        return NULL;
    }

    CollectDelData data = {0};
    GHashTable *entries = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, g_object_unref);
    data.repo = repo;
    data.entries = entries;
    data.truncate_time = MAX (show_time, truncate_time);
    if (path) {
        if (path[strlen(path) - 1] == '/') {
            data.path = g_strdup (path);
        } else {
            data.path = g_strconcat (path, "/", NULL);
        }
    } else {
        data.path = g_strdup ("/");
    }

    if (!scan_commits_for_collect_deleted (&data, scan_stat, limit, &next_scan_stat)) {
        g_set_error (error, SYNCWERK_DOMAIN, SYNCW_ERR_INTERNAL,
                     "Internal error");
        g_hash_table_destroy (entries);
        syncw_repo_unref (repo);
        g_free (data.path);
        return NULL;
    }

    /* Remove entries exist in the current commit.
     * This is necessary because some files may be added back after deletion.
     */
    if (filter_out_existing_entries (entries, repo,
                                     repo->head->commit_id) == 0) {
        // filter success, then add collected result to list
        g_hash_table_foreach_steal (entries, hash_to_list, &ret);
    }

    // Append scan_stat entry to the end to indicate the end of scan result
    ret = g_list_append (ret, g_object_new (SYNCWERK_TYPE_DELETED_ENTRY,
                                            "scan_stat", next_scan_stat,
                                            NULL));

    g_hash_table_destroy (entries);

    syncw_repo_unref (repo);
    g_free (data.path);

    return ret;
}

static SyncwCommit *
get_commit(SyncwRepo *repo, const char *branch_or_commit)
{
    SyncwBranch *b;
    SyncwCommit *c;

    b = syncw_branch_manager_get_branch (syncw->branch_mgr, repo->id,
                                        branch_or_commit);
    if (!b) {
        if (strcmp(branch_or_commit, "HEAD") == 0)
            c = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                                repo->id, repo->version, 
                                                repo->head->commit_id);
        else
            c = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                                repo->id, repo->version, 
                                                branch_or_commit);
    } else {
        c = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                            repo->id, repo->version, 
                                            b->commit_id);
    }

    if (b)
        syncw_branch_unref (b);
    
    return c;
}

GList *
syncw_repo_diff (SyncwRepo *repo, const char *old, const char *new, int fold_dir_results, char **error)
{
    SyncwCommit *c1 = NULL, *c2 = NULL;
    int ret = 0;
    GList *diff_entries = NULL;

    g_return_val_if_fail (*error == NULL, NULL);

    c2 = get_commit (repo, new);
    if (!c2) {
        *error = g_strdup("Can't find new commit");
        return NULL;
    }
    
    if (old == NULL || old[0] == '\0') {
        if (c2->parent_id && c2->second_parent_id) {
            ret = diff_merge (c2, &diff_entries, fold_dir_results);
            syncw_commit_unref (c2);
            if (ret < 0) {
                *error = g_strdup("Failed to do diff");
                g_list_free_full (diff_entries, (GDestroyNotify)diff_entry_free);
                return NULL;
            }
            return diff_entries;
        }

        if (!c2->parent_id) {
            syncw_commit_unref (c2);
            return NULL;
        }
        c1 = syncw_commit_manager_get_commit (syncw->commit_mgr,
                                             repo->id, repo->version, 
                                             c2->parent_id);
    } else {
        c1 = get_commit (repo, old);
    }

    if (!c1) {
        *error = g_strdup("Can't find old commit");
        syncw_commit_unref (c2);
        return NULL;
    }

    /* do diff */
    ret = diff_commits (c1, c2, &diff_entries, fold_dir_results);
    if (ret < 0) {
        g_list_free_full (diff_entries, (GDestroyNotify)diff_entry_free);
        diff_entries = NULL;
        *error = g_strdup("Failed to do diff");
    }

    syncw_commit_unref (c1);
    syncw_commit_unref (c2);

    return diff_entries;
}

