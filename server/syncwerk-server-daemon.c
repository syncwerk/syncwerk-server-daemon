/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>

#include <glib.h>
#include <glib-object.h>

#include <ccnet.h>
#include <rpcsyncwerk-server.h>
#include <rpcsyncwerk-client.h>

#include "syncwerk-session.h"
#include "syncwerk-rpc.h"
#include <ccnet/rpcserver-proc.h>
#include <ccnet/threaded-rpcserver-proc.h>
#include "log.h"
#include "utils.h"

#include "processors/check-tx-slave-v3-proc.h"
#include "processors/recvfs-proc.h"
#include "processors/putfs-proc.h"
#include "processors/recvbranch-proc.h"
#include "processors/sync-repo-slave-proc.h"
#include "processors/putcommit-v2-proc.h"
#include "processors/putcommit-v3-proc.h"
#include "processors/recvcommit-v3-proc.h"
#include "processors/putcs-v2-proc.h"
#include "processors/checkbl-proc.h"
#include "processors/checkff-proc.h"
#include "processors/putca-proc.h"
#include "processors/check-protocol-slave-proc.h"
#include "processors/recvfs-v2-proc.h"
#include "processors/recvbranch-v2-proc.h"
#include "processors/putfs-v2-proc.h"

#include "cdc/cdc.h"

SyncwerkSession *syncw;
RpcsyncwerkClient *ccnetrpc_client;
RpcsyncwerkClient *ccnetrpc_client_t;
RpcsyncwerkClient *async_ccnetrpc_client;
RpcsyncwerkClient *async_ccnetrpc_client_t;

char *pidfile = NULL;

static const char *short_options = "hvc:d:l:fg:G:P:mCD:F:";
static struct option long_options[] = {
    { "help", no_argument, NULL, 'h', },
    { "version", no_argument, NULL, 'v', },
    { "config-file", required_argument, NULL, 'c' },
    { "central-config-dir", required_argument, NULL, 'F' },
    { "syncwdir", required_argument, NULL, 'd' },
    { "log", required_argument, NULL, 'l' },
    { "debug", required_argument, NULL, 'D' },
    { "foreground", no_argument, NULL, 'f' },
    { "ccnet-debug-level", required_argument, NULL, 'g' },
    { "syncwerk-debug-level", required_argument, NULL, 'G' },
    { "master", no_argument, NULL, 'm'},
    { "pidfile", required_argument, NULL, 'P' },
    { "cloud-mode", no_argument, NULL, 'C'},
    { NULL, 0, NULL, 0, },
};

static void usage ()
{
    fprintf (stderr, "usage: syncwerk-server-daemon [-c config_dir] [-d syncwerk_dir]\n");
}

static void register_processors (CcnetClient *client)
{
    ccnet_register_service (client, "syncwerk-check-tx-slave-v3", "basic",
                            SYNCWERK_TYPE_CHECK_TX_SLAVE_V3_PROC, NULL);
    ccnet_register_service (client, "syncwerk-recvfs", "basic",
                            SYNCWERK_TYPE_RECVFS_PROC, NULL);
    ccnet_register_service (client, "syncwerk-putfs", "basic",
                            SYNCWERK_TYPE_PUTFS_PROC, NULL);
    ccnet_register_service (client, "syncwerk-recvbranch", "basic",
                            SYNCWERK_TYPE_RECVBRANCH_PROC, NULL);
    ccnet_register_service (client, "syncwerk-sync-repo-slave", "basic",
                            SYNCWERK_TYPE_SYNC_REPO_SLAVE_PROC, NULL);
    ccnet_register_service (client, "syncwerk-putcommit-v2", "basic",
                            SYNCWERK_TYPE_PUTCOMMIT_V2_PROC, NULL);
    ccnet_register_service (client, "syncwerk-putcommit-v3", "basic",
                            SYNCWERK_TYPE_PUTCOMMIT_V3_PROC, NULL);
    ccnet_register_service (client, "syncwerk-recvcommit-v3", "basic",
                            SYNCWERK_TYPE_RECVCOMMIT_V3_PROC, NULL);
    ccnet_register_service (client, "syncwerk-putcs-v2", "basic",
                            SYNCWERK_TYPE_PUTCS_V2_PROC, NULL);
    ccnet_register_service (client, "syncwerk-checkbl", "basic",
                            SYNCWERK_TYPE_CHECKBL_PROC, NULL);
    ccnet_register_service (client, "syncwerk-checkff", "basic",
                            SYNCWERK_TYPE_CHECKFF_PROC, NULL);
    ccnet_register_service (client, "syncwerk-putca", "basic",
                            SYNCWERK_TYPE_PUTCA_PROC, NULL);
    ccnet_register_service (client, "syncwerk-check-protocol-slave", "basic",
                            SYNCWERK_TYPE_CHECK_PROTOCOL_SLAVE_PROC, NULL);
    ccnet_register_service (client, "syncwerk-recvfs-v2", "basic",
                            SYNCWERK_TYPE_RECVFS_V2_PROC, NULL);
    ccnet_register_service (client, "syncwerk-recvbranch-v2", "basic",
                            SYNCWERK_TYPE_RECVBRANCH_V2_PROC, NULL);
    ccnet_register_service (client, "syncwerk-putfs-v2", "basic",
                            SYNCWERK_TYPE_PUTFS_V2_PROC, NULL);
}

#include <rpcsyncwerk.h>
#include "rpcsyncwerk-signature.h"
#include "rpcsyncwerk-marshal.h"

static void start_rpc_service (CcnetClient *client, int cloud_mode)
{
    rpcsyncwerk_server_init (register_marshals);

    rpcsyncwerk_create_service ("syncwserv-rpcserver");
    ccnet_register_service (client, "syncwserv-rpcserver", "rpc-inner",
                            CCNET_TYPE_RPCSERVER_PROC, NULL);

    rpcsyncwerk_create_service ("syncwserv-threaded-rpcserver");
    ccnet_register_service (client, "syncwserv-threaded-rpcserver", "rpc-inner",
                            CCNET_TYPE_THREADED_RPCSERVER_PROC, NULL);

    /* threaded services */

    /* repo manipulation */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_repo,
                                     "syncwerk_get_repo",
                                     rpcsyncwerk_signature_object__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_destroy_repo,
                                     "syncwerk_destroy_repo",
                                     rpcsyncwerk_signature_int__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_repo_list,
                                     "syncwerk_get_repo_list",
                                     rpcsyncwerk_signature_objlist__int_int());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_count_repos,
                                     "syncwerk_count_repos",
                                     rpcsyncwerk_signature_int64__void());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_repo_owner,
                                     "syncwerk_set_repo_owner",
                                     rpcsyncwerk_signature_int__string_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_repo_owner,
                                     "syncwerk_get_repo_owner",
                                     rpcsyncwerk_signature_string__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_orphan_repo_list,
                                     "syncwerk_get_orphan_repo_list",
                                     rpcsyncwerk_signature_objlist__void());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_edit_repo,
                                     "syncwerk_edit_repo",
                                     rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_change_repo_passwd,
                                     "syncwerk_change_repo_passwd",
                                     rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_is_repo_owner,
                                     "syncwerk_is_repo_owner",
                                     rpcsyncwerk_signature_int__string_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_owned_repos,
                                     "syncwerk_list_owned_repos",
                                     rpcsyncwerk_signature_objlist__string_int_int_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_server_repo_size,
                                     "syncwerk_server_repo_size",
                                     rpcsyncwerk_signature_int64__string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_repo_set_access_property,
                                     "syncwerk_repo_set_access_property",
                                     rpcsyncwerk_signature_int__string_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_repo_query_access_property,
                                     "syncwerk_repo_query_access_property",
                                     rpcsyncwerk_signature_string__string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_revert_on_server,
                                     "syncwerk_revert_on_server",
                                     rpcsyncwerk_signature_int__string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_diff,
                                     "syncwerk_diff",
                                     rpcsyncwerk_signature_objlist__string_string_string_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_post_file,
                                     "syncwerk_post_file",
                    rpcsyncwerk_signature_int__string_string_string_string_string());

    /* rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver", */
    /*                                  syncwerk_post_file_blocks, */
    /*                                  "syncwerk_post_file_blocks", */
    /*                 rpcsyncwerk_signature_string__string_string_string_string_string_string_int64_int()); */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_post_multi_files,
                                     "syncwerk_post_multi_files",
                    rpcsyncwerk_signature_string__string_string_string_string_string_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_put_file,
                                     "syncwerk_put_file",
                    rpcsyncwerk_signature_string__string_string_string_string_string_string());
    /* rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver", */
    /*                                  syncwerk_put_file_blocks, */
    /*                                  "syncwerk_put_file_blocks", */
    /*                 rpcsyncwerk_signature_string__string_string_string_string_string_string_string_int64()); */

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_post_empty_file,
                                     "syncwerk_post_empty_file",
                        rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_post_dir,
                                     "syncwerk_post_dir",
                        rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_mkdir_with_parents,
                                     "syncwerk_mkdir_with_parents",
                        rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_del_file,
                                     "syncwerk_del_file",
                        rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_copy_file,
                                     "syncwerk_copy_file",
       rpcsyncwerk_signature_object__string_string_string_string_string_string_string_int_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_move_file,
                                     "syncwerk_move_file",
       rpcsyncwerk_signature_object__string_string_string_string_string_string_int_string_int_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_rename_file,
                                     "syncwerk_rename_file",
                    rpcsyncwerk_signature_int__string_string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_is_valid_filename,
                                     "syncwerk_is_valid_filename",
                                     rpcsyncwerk_signature_int__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_create_repo,
                                     "syncwerk_create_repo",
                                     rpcsyncwerk_signature_string__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_create_enc_repo,
                                     "syncwerk_create_enc_repo",
                                     rpcsyncwerk_signature_string__string_string_string_string_string_string_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_commit,
                                     "syncwerk_get_commit",
                                     rpcsyncwerk_signature_object__string_int_string());
    
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_dir,
                                     "syncwerk_list_dir",
                                     rpcsyncwerk_signature_objlist__string_string_int_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_dir_with_perm,
                                     "list_dir_with_perm",
                                     rpcsyncwerk_signature_objlist__string_string_string_string_int_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_file_blocks,
                                     "syncwerk_list_file_blocks",
                                     rpcsyncwerk_signature_string__string_string_int_int());
    
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_file_size,
                                     "syncwerk_get_file_size",
                                     rpcsyncwerk_signature_int64__string_int_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_dir_size,
                                     "syncwerk_get_dir_size",
                                     rpcsyncwerk_signature_int64__string_int_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_dir_by_path,
                                     "syncwerk_list_dir_by_path",
                                     rpcsyncwerk_signature_objlist__string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_dir_id_by_commit_and_path,
                                     "syncwerk_get_dir_id_by_commit_and_path",
                                     rpcsyncwerk_signature_string__string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_file_id_by_path,
                                     "syncwerk_get_file_id_by_path",
                                     rpcsyncwerk_signature_string__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_dir_id_by_path,
                                     "syncwerk_get_dir_id_by_path",
                                     rpcsyncwerk_signature_string__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_dirent_by_path,
                                     "syncwerk_get_dirent_by_path",
                                     rpcsyncwerk_signature_object__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_file_revisions,
                                     "syncwerk_list_file_revisions",
                                     rpcsyncwerk_signature_objlist__string_string_string_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_calc_files_last_modified,
                                     "syncwerk_calc_files_last_modified",
                                     rpcsyncwerk_signature_objlist__string_string_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_revert_file,
                                     "syncwerk_revert_file",
                                     rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_revert_dir,
                                     "syncwerk_revert_dir",
                                     rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_check_repo_blocks_missing,
                                     "syncwerk_check_repo_blocks_missing",
                                     rpcsyncwerk_signature_string__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_deleted,
                                     "get_deleted",
                                     rpcsyncwerk_signature_objlist__string_int_string_string_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_total_file_number,
                                     "get_total_file_number",
                                     rpcsyncwerk_signature_int64__void());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_total_storage,
                                     "get_total_storage",
                                     rpcsyncwerk_signature_int64__void());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_file_count_info_by_path,
                                     "get_file_count_info_by_path",
                                     rpcsyncwerk_signature_object__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_trash_repo_owner,
                                     "get_trash_repo_owner",
                                     rpcsyncwerk_signature_string__string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_convert_repo_path,
                                     "convert_repo_path",
                                     rpcsyncwerk_signature_string__string_string_string_int());

    /* share repo to user */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_add_share,
                                     "syncwerk_add_share",
                                     rpcsyncwerk_signature_int__string_string_string_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_share_repos,
                                     "syncwerk_list_share_repos",
                                     rpcsyncwerk_signature_objlist__string_string_int_int());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_repo_shared_to,
                                     "syncwerk_list_repo_shared_to",
                                     rpcsyncwerk_signature_objlist__string_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_remove_share,
                                     "syncwerk_remove_share",
                                     rpcsyncwerk_signature_int__string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_share_permission,
                                     "set_share_permission",
                                     rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_share_subdir_to_user,
                                     "share_subdir_to_user",
                                     rpcsyncwerk_signature_string__string_string_string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_unshare_subdir_for_user,
                                     "unshare_subdir_for_user",
                                     rpcsyncwerk_signature_int__string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_update_share_subdir_perm_for_user,
                                     "update_share_subdir_perm_for_user",
                                     rpcsyncwerk_signature_int__string_string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_shared_repo_by_path,
                                     "get_shared_repo_by_path",
                                     rpcsyncwerk_signature_object__string_string_string_int());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_shared_users_by_repo,
                                     "get_shared_users_by_repo",
                                     rpcsyncwerk_signature_objlist__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_org_get_shared_users_by_repo,
                                     "org_get_shared_users_by_repo",
                                     rpcsyncwerk_signature_objlist__int_string());

    /* share repo to group */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_group_share_repo,
                                     "syncwerk_group_share_repo",
                                     rpcsyncwerk_signature_int__string_int_string_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_group_unshare_repo,
                                     "syncwerk_group_unshare_repo",
                                     rpcsyncwerk_signature_int__string_int_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_shared_groups_by_repo,
                                     "syncwerk_get_shared_groups_by_repo",
                                     rpcsyncwerk_signature_string__string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_share_subdir_to_group,
                                     "share_subdir_to_group",
                                     rpcsyncwerk_signature_string__string_string_string_int_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_unshare_subdir_for_group,
                                     "unshare_subdir_for_group",
                                     rpcsyncwerk_signature_int__string_string_string_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_update_share_subdir_perm_for_group,
                                     "update_share_subdir_perm_for_group",
                                     rpcsyncwerk_signature_int__string_string_string_int_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_group_repoids,
                                     "syncwerk_get_group_repoids",
                                     rpcsyncwerk_signature_string__int());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_repo_shared_group,
                                     "syncwerk_list_repo_shared_group",
                                     rpcsyncwerk_signature_objlist__string_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_group_shared_repo_by_path,
                                     "get_group_shared_repo_by_path",
                                     rpcsyncwerk_signature_object__string_string_int_int());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_group_repos_by_user,
                                     "get_group_repos_by_user",
                                     rpcsyncwerk_signature_objlist__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_org_group_repos_by_user,
                                     "get_org_group_repos_by_user",
                                     rpcsyncwerk_signature_objlist__string_int());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_repos_by_group,
                                     "syncwerk_get_repos_by_group",
                                     rpcsyncwerk_signature_objlist__int());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_group_repos_by_owner,
                                     "get_group_repos_by_owner",
                                     rpcsyncwerk_signature_objlist__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_group_repo_owner,
                                     "get_group_repo_owner",
                                     rpcsyncwerk_signature_string__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_remove_repo_group,
                                     "syncwerk_remove_repo_group",
                                     rpcsyncwerk_signature_int__int_string());    

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_group_repo_permission,
                                     "set_group_repo_permission",
                                     rpcsyncwerk_signature_int__int_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_shared_users_for_subdir,
                                     "syncwerk_get_shared_users_for_subdir",
                                     rpcsyncwerk_signature_objlist__string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_shared_groups_for_subdir,
                                     "syncwerk_get_shared_groups_for_subdir",
                                     rpcsyncwerk_signature_objlist__string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_repo_has_been_shared,
                                     "repo_has_been_shared",
                                     rpcsyncwerk_signature_int__string_int());

    /* branch and commit */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_branch_gets,
                                     "syncwerk_branch_gets",
                                     rpcsyncwerk_signature_objlist__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_commit_list,
                                     "syncwerk_get_commit_list",
                                     rpcsyncwerk_signature_objlist__string_int_int());

    /* token */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_generate_repo_token,
                                     "syncwerk_generate_repo_token",
                                     rpcsyncwerk_signature_string__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_delete_repo_token,
                                     "syncwerk_delete_repo_token",
                                     rpcsyncwerk_signature_int__string_string_string());
    
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_repo_tokens,
                                     "syncwerk_list_repo_tokens",
                                     rpcsyncwerk_signature_objlist__string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_list_repo_tokens_by_email,
                                     "syncwerk_list_repo_tokens_by_email",
                                     rpcsyncwerk_signature_objlist__string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_delete_repo_tokens_by_peer_id,
                                     "syncwerk_delete_repo_tokens_by_peer_id",
                                     rpcsyncwerk_signature_int__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_delete_repo_tokens_by_email,
                                     "delete_repo_tokens_by_email",
                                     rpcsyncwerk_signature_int__string());
    
    /* quota */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_user_quota_usage,
                                     "syncwerk_get_user_quota_usage",
                                     rpcsyncwerk_signature_int64__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_user_share_usage,
                                     "syncwerk_get_user_share_usage",
                                     rpcsyncwerk_signature_int64__string());

    /* virtual repo */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_create_virtual_repo,
                                     "create_virtual_repo",
                                     rpcsyncwerk_signature_string__string_string_string_string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_virtual_repos_by_owner,
                                     "get_virtual_repos_by_owner",
                                     rpcsyncwerk_signature_objlist__string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_virtual_repo,
                                     "get_virtual_repo",
                                     rpcsyncwerk_signature_object__string_string_string());

    /* Clean trash */

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_clean_up_repo_history,
                                     "clean_up_repo_history",
                                     rpcsyncwerk_signature_int__string_int());

    /* -------- rpc services -------- */
    /* token for web access to repo */
    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_web_get_access_token,
                                     "syncwerk_web_get_access_token",
                                     rpcsyncwerk_signature_string__string_string_string_string_int());
    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_web_query_access_token,
                                     "syncwerk_web_query_access_token",
                                     rpcsyncwerk_signature_object__string());

    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_query_zip_progress,
                                     "syncwerk_query_zip_progress",
                                     rpcsyncwerk_signature_string__string());
    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_cancel_zip_task,
                                     "cancel_zip_task",
                                     rpcsyncwerk_signature_int__string());

    /* Copy task related. */

    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_get_copy_task,
                                     "get_copy_task",
                                     rpcsyncwerk_signature_object__string());

    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_cancel_copy_task,
                                     "cancel_copy_task",
                                     rpcsyncwerk_signature_int__string());

    /* chunk server manipulation */
    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_add_chunk_server,
                                     "syncwerk_add_chunk_server",
                                     rpcsyncwerk_signature_int__string());
    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_del_chunk_server,
                                     "syncwerk_del_chunk_server",
                                     rpcsyncwerk_signature_int__string());
    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_list_chunk_servers,
                                     "syncwerk_list_chunk_servers",
                                     rpcsyncwerk_signature_string__void());

    /* password management */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_check_passwd,
                                     "syncwerk_check_passwd",
                                     rpcsyncwerk_signature_int__string_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_passwd,
                                     "syncwerk_set_passwd",
                                     rpcsyncwerk_signature_int__string_string_string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_unset_passwd,
                                     "syncwerk_unset_passwd",
                                     rpcsyncwerk_signature_int__string_string());
    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_is_passwd_set,
                                     "syncwerk_is_passwd_set",
                                     rpcsyncwerk_signature_int__string_string());
    rpcsyncwerk_server_register_function ("syncwserv-rpcserver",
                                     syncwerk_get_decrypt_key,
                                     "syncwerk_get_decrypt_key",
                                     rpcsyncwerk_signature_object__string_string());

    /* quota management */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_user_quota,
                                     "set_user_quota",
                                     rpcsyncwerk_signature_int__string_int64());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_user_quota,
                                     "get_user_quota",
                                     rpcsyncwerk_signature_int64__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_check_quota,
                                     "check_quota",
                                     rpcsyncwerk_signature_int__string_int64());

    /* repo permission */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_check_permission,
                                     "check_permission",
                                     rpcsyncwerk_signature_string__string_string());

    /* folder permission */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_check_permission_by_path,
                                     "check_permission_by_path",
                                     rpcsyncwerk_signature_string__string_string_string());
    
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_file_id_by_commit_and_path,
                                     "syncwerk_get_file_id_by_commit_and_path",
                                     rpcsyncwerk_signature_string__string_string_string());

    if (!cloud_mode) {
        rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                         syncwerk_set_inner_pub_repo,
                                         "set_inner_pub_repo",
                                         rpcsyncwerk_signature_int__string_string());
        rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                         syncwerk_unset_inner_pub_repo,
                                         "unset_inner_pub_repo",
                                         rpcsyncwerk_signature_int__string());
        rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                         syncwerk_is_inner_pub_repo,
                                         "is_inner_pub_repo",
                                         rpcsyncwerk_signature_int__string());
        rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                         syncwerk_list_inner_pub_repos,
                                         "list_inner_pub_repos",
                                         rpcsyncwerk_signature_objlist__void());
        rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                         syncwerk_count_inner_pub_repos,
                                         "count_inner_pub_repos",
                                         rpcsyncwerk_signature_int64__void());
        rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                         syncwerk_list_inner_pub_repos_by_owner,
                                         "list_inner_pub_repos_by_owner",
                                         rpcsyncwerk_signature_objlist__string());
    }

    /* History */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_repo_history_limit,
                                     "set_repo_history_limit",
                                     rpcsyncwerk_signature_int__string_int());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_repo_history_limit,
                                     "get_repo_history_limit",
                                     rpcsyncwerk_signature_int__string());

    /* System default library */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_system_default_repo_id,
                                     "get_system_default_repo_id",
                                     rpcsyncwerk_signature_string__void());

    /* Trashed repos. */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_trash_repo_list,
                                     "get_trash_repo_list",
                                     rpcsyncwerk_signature_objlist__int_int());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_del_repo_from_trash,
                                     "del_repo_from_trash",
                                     rpcsyncwerk_signature_int__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_restore_repo_from_trash,
                                     "restore_repo_from_trash",
                                     rpcsyncwerk_signature_int__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_trash_repos_by_owner,
                                     "get_trash_repos_by_owner",
                                     rpcsyncwerk_signature_objlist__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_empty_repo_trash,
                                     "empty_repo_trash",
                                     rpcsyncwerk_signature_int__void());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_empty_repo_trash_by_owner,
                                     "empty_repo_trash_by_owner",
                                     rpcsyncwerk_signature_int__string());
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_generate_magic_and_random_key,
                                     "generate_magic_and_random_key",
                                     rpcsyncwerk_signature_object__int_string_string());

    /* Config */
    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_server_config_int,
                                     "get_server_config_int",
                                     rpcsyncwerk_signature_int__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_server_config_int,
                                     "set_server_config_int",
                                     rpcsyncwerk_signature_int__string_string_int());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_server_config_int64,
                                     "get_server_config_int64",
                                     rpcsyncwerk_signature_int64__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_server_config_int64,
                                     "set_server_config_int64",
                                     rpcsyncwerk_signature_int__string_string_int64());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_server_config_string,
                                     "get_server_config_string",
                                     rpcsyncwerk_signature_string__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_server_config_string,
                                     "set_server_config_string",
                                     rpcsyncwerk_signature_int__string_string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_get_server_config_boolean,
                                     "get_server_config_boolean",
                                     rpcsyncwerk_signature_int__string_string());

    rpcsyncwerk_server_register_function ("syncwserv-threaded-rpcserver",
                                     syncwerk_set_server_config_boolean,
                                     "set_server_config_boolean",
                                     rpcsyncwerk_signature_int__string_string_int());

}

static struct event sigusr1;

static void sigusr1Handler (int fd, short event, void *user_data)
{
    syncwerk_log_reopen ();
}

static void
set_signal_handlers (SyncwerkSession *session)
{
#ifndef WIN32
    signal (SIGPIPE, SIG_IGN);

    /* design as reopen log */
    event_set(&sigusr1, SIGUSR1, EV_SIGNAL | EV_PERSIST, sigusr1Handler, NULL);
    event_add(&sigusr1, NULL);
#endif
}

static void
create_sync_rpc_clients (const char *central_config_dir, const char *config_dir)
{
    CcnetClient *sync_client;

    /* sync client and rpc client */
    sync_client = ccnet_client_new ();
    if ( (ccnet_client_load_confdir(sync_client, central_config_dir, config_dir)) < 0 ) {
        syncw_warning ("Read config dir error\n");
        exit(1);
    }

    if (ccnet_client_connect_daemon (sync_client, CCNET_CLIENT_SYNC) < 0)
    {
        syncw_warning ("Connect to server fail: %s\n", strerror(errno));
        exit(1);
    }

    ccnetrpc_client = ccnet_create_rpc_client (sync_client, NULL, "ccnet-rpcserver");
    ccnetrpc_client_t = ccnet_create_rpc_client (sync_client,
                                                 NULL,
                                                 "ccnet-threaded-rpcserver");
}

static void
create_async_rpc_clients (CcnetClient *client)
{
    async_ccnetrpc_client = ccnet_create_async_rpc_client (
        client, NULL, "ccnet-rpcserver");
    async_ccnetrpc_client_t = ccnet_create_async_rpc_client (
        client, NULL, "ccnet-threaded-rpcserver");
}

static void
remove_pidfile (const char *pidfile)
{
    if (pidfile) {
        g_unlink (pidfile);
    }
}

static int
write_pidfile (const char *pidfile_path)
{
    if (!pidfile_path)
        return -1;

    pid_t pid = getpid();

    FILE *pidfile = g_fopen(pidfile_path, "w");
    if (!pidfile) {
        syncw_warning ("Failed to fopen() pidfile %s: %s\n",
                      pidfile_path, strerror(errno));
        return -1;
    }

    char buf[32];
    snprintf (buf, sizeof(buf), "%d\n", pid);
    if (fputs(buf, pidfile) < 0) {
        syncw_warning ("Failed to write pidfile %s: %s\n",
                      pidfile_path, strerror(errno));
        fclose (pidfile);
        return -1;
    }

    fflush (pidfile);
    fclose (pidfile);
    return 0;
}

static void
on_syncwerk_server_daemon_exit(void)
{
    if (pidfile)
        remove_pidfile (pidfile);
}

#ifdef WIN32
/* Get the commandline arguments in unicode, then convert them to utf8  */
static char **
get_argv_utf8 (int *argc)
{
    int i = 0;
    char **argv = NULL;
    const wchar_t *cmdline = NULL;
    wchar_t **argv_w = NULL;

    cmdline = GetCommandLineW();
    argv_w = CommandLineToArgvW (cmdline, argc);
    if (!argv_w) {
        printf("failed to CommandLineToArgvW(), GLE=%lu\n", GetLastError());
        return NULL;
    }

    argv = (char **)malloc (sizeof(char*) * (*argc));
    for (i = 0; i < *argc; i++) {
        argv[i] = wchar_to_utf8 (argv_w[i]);
    }

    return argv;
}
#endif

int
main (int argc, char **argv)
{
    int c;
    char *config_dir = DEFAULT_CONFIG_DIR;
    char *syncwerk_dir = NULL;
    char *central_config_dir = NULL;
    char *logfile = NULL;
    const char *debug_str = NULL;
    int daemon_mode = 1;
    int is_master = 0;
    CcnetClient *client;
    char *ccnet_debug_level_str = "info";
    char *syncwerk_debug_level_str = "debug";
    int cloud_mode = 0;

#ifdef WIN32
    argv = get_argv_utf8 (&argc);
#endif

    while ((c = getopt_long (argc, argv, short_options, 
                             long_options, NULL)) != EOF)
    {
        switch (c) {
        case 'h':
            exit (1);
            break;
        case 'v':
            exit (1);
            break;
        case 'c':
            config_dir = optarg;
            break;
        case 'd':
            syncwerk_dir = g_strdup(optarg);
            break;
        case 'F':
            central_config_dir = g_strdup(optarg);
            break;
        case 'f':
            daemon_mode = 0;
            break;
        case 'l':
            logfile = g_strdup(optarg);
            break;
        case 'D':
            debug_str = optarg;
            break;
        case 'g':
            ccnet_debug_level_str = optarg;
            break;
        case 'G':
            syncwerk_debug_level_str = optarg;
            break;
        case 'm':
            is_master = 1;
            break;
        case 'P':
            pidfile = optarg;
            break;
        case 'C':
            cloud_mode = 1;
            break;
        default:
            usage ();
            exit (1);
        }
    }

    argc -= optind;
    argv += optind;

#ifndef WIN32
    if (daemon_mode) {
#ifndef __APPLE__
        daemon (1, 0);
#else   /* __APPLE */
        /* daemon is deprecated under APPLE
         * use fork() instead
         * */
        switch (fork ()) {
          case -1:
              syncw_warning ("Failed to daemonize");
              exit (-1);
              break;
          case 0:
              /* all good*/
              break;
          default:
              /* kill origin process */
              exit (0);
        }
#endif  /* __APPLE */
    }
#endif /* !WIN32 */

    cdc_init ();

#if !GLIB_CHECK_VERSION(2, 35, 0)
    g_type_init();
#endif
#if !GLIB_CHECK_VERSION(2,32,0)
    g_thread_init (NULL);
#endif

    if (!debug_str)
        debug_str = g_getenv("SYNCWERK_DEBUG");
    syncwerk_debug_set_flags_string (debug_str);

    if (syncwerk_dir == NULL)
        syncwerk_dir = g_build_filename (config_dir, "syncwerk", NULL);
    if (logfile == NULL)
        logfile = g_build_filename (syncwerk_dir, "syncwerk.log", NULL);

    if (syncwerk_log_init (logfile, ccnet_debug_level_str,
                          syncwerk_debug_level_str) < 0) {
        syncw_warning ("Failed to init log.\n");
        exit (1);
    }

    client = ccnet_init (central_config_dir, config_dir);
    if (!client)
        exit (1);

    register_processors (client);

    start_rpc_service (client, cloud_mode);

    create_sync_rpc_clients (central_config_dir, config_dir);
    create_async_rpc_clients (client);

    syncw = syncwerk_session_new (central_config_dir, syncwerk_dir, client);
    if (!syncw) {
        syncw_warning ("Failed to create syncwerk session.\n");
        exit (1);
    }
    syncw->is_master = is_master;
    syncw->ccnetrpc_client = ccnetrpc_client;
    syncw->async_ccnetrpc_client = async_ccnetrpc_client;
    syncw->ccnetrpc_client_t = ccnetrpc_client_t;
    syncw->async_ccnetrpc_client_t = async_ccnetrpc_client_t;
    syncw->client_pool = ccnet_client_pool_new (central_config_dir, config_dir);
    syncw->cloud_mode = cloud_mode;

#ifndef WIN32
    set_syslog_config (syncw->config);
#endif

    g_free (syncwerk_dir);
    g_free (logfile);

    set_signal_handlers (syncw);

    /* Create pid file before connecting to database.
     * Connecting to database and creating tables may take long if the db
     * is on a remote host. This may make controller think syncwerk-server-daemon fails
     * to start and restart it.
     */
    if (pidfile) {
        if (write_pidfile (pidfile) < 0) {
            ccnet_message ("Failed to write pidfile\n");
            return -1;
        }
    }

    /* init syncw */
    if (syncwerk_session_init (syncw) < 0)
        exit (1);

    if (syncwerk_session_start (syncw) < 0)
        exit (1);

    atexit (on_syncwerk_server_daemon_exit);

    /* Create a system default repo to contain the tutorial file. */
    schedule_create_system_default_repo (syncw);

    ccnet_main (client);

    return 0;
}
