
import ccnet
from pyrpcsyncwerk import rpcsyncwerk_func, RpcsyncwerkError

class SyncwerkRpcClient(ccnet.RpcClientBase):
    """RPC used in client"""

    def __init__(self, ccnet_client_pool, *args, **kwargs):
        ccnet.RpcClientBase.__init__(self, ccnet_client_pool, "syncwerk-rpcserver",
                                     *args, **kwargs)

    @rpcsyncwerk_func("object", [])
    def syncwerk_get_session_info():
        pass
    get_session_info = syncwerk_get_session_info

    @rpcsyncwerk_func("int", ["string"])
    def syncwerk_calc_dir_size(path):
        pass
    calc_dir_size = syncwerk_calc_dir_size

    @rpcsyncwerk_func("int64", [])
    def syncwerk_get_total_block_size():
        pass
    get_total_block_size = syncwerk_get_total_block_size;

    @rpcsyncwerk_func("string", ["string"])
    def syncwerk_get_config(key):
        pass
    get_config = syncwerk_get_config

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_set_config(key, value):
        pass
    set_config = syncwerk_set_config

    @rpcsyncwerk_func("int", ["string"])
    def syncwerk_get_config_int(key):
        pass
    get_config_int = syncwerk_get_config_int

    @rpcsyncwerk_func("int", ["string", "int"])
    def syncwerk_set_config_int(key, value):
        pass
    set_config_int = syncwerk_set_config_int

    @rpcsyncwerk_func("int", ["int"])
    def syncwerk_set_upload_rate_limit(limit):
        pass
    set_upload_rate_limit = syncwerk_set_upload_rate_limit

    @rpcsyncwerk_func("int", ["int"])
    def syncwerk_set_download_rate_limit(limit):
        pass
    set_download_rate_limit = syncwerk_set_download_rate_limit

    ### repo
    @rpcsyncwerk_func("objlist", ["int", "int"])
    def syncwerk_get_repo_list():
        pass
    get_repo_list = syncwerk_get_repo_list

    @rpcsyncwerk_func("object", ["string"])
    def syncwerk_get_repo():
        pass
    get_repo = syncwerk_get_repo

    @rpcsyncwerk_func("string", ["string", "string", "string", "string", "string", "int"])
    def syncwerk_create_repo(name, desc, passwd, base, relay_id, keep_history):
        pass
    create_repo = syncwerk_create_repo

    @rpcsyncwerk_func("int", ["string"])
    def syncwerk_destroy_repo(repo_id):
        pass
    remove_repo = syncwerk_destroy_repo

    @rpcsyncwerk_func("objlist", ["string", "string", "string", "int"])
    def syncwerk_diff():
        pass
    get_diff = syncwerk_diff

    @rpcsyncwerk_func("object", ["string", "int", "string"])
    def syncwerk_get_commit(repo_id, version, commit_id):
        pass
    get_commit = syncwerk_get_commit

    @rpcsyncwerk_func("objlist", ["string", "int", "int"])
    def syncwerk_get_commit_list():
        pass
    get_commit_list = syncwerk_get_commit_list

    @rpcsyncwerk_func("objlist", ["string"])
    def syncwerk_branch_gets(repo_id):
        pass
    branch_gets = syncwerk_branch_gets

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_branch_add(repo_id, branch):
        pass
    branch_add = syncwerk_branch_add

    ##### clone related
    @rpcsyncwerk_func("string", ["string", "string"])
    def gen_default_worktree(worktree_parent, repo_name):
        pass

    @rpcsyncwerk_func("string", ["string", "int", "string", "string", "string", "string", "string", "string", "string", "string", "string", "int", "string"])
    def syncwerk_clone(repo_id, repo_version, peer_id, repo_name, worktree, token, password, magic, peer_addr, peer_port, email, random_key, enc_version, more_info):
        pass
    clone = syncwerk_clone

    @rpcsyncwerk_func("string", ["string", "int", "string", "string", "string", "string", "string", "string", "string", "string", "string", "int", "string"])
    def syncwerk_download(repo_id, repo_version, peer_id, repo_name, wt_parent, token, password, magic, peer_addr, peer_port, email, random_key, enc_version, more_info):
        pass
    download = syncwerk_download

    @rpcsyncwerk_func("int", ["string"])
    def syncwerk_cancel_clone_task(repo_id):
        pass
    cancel_clone_task = syncwerk_cancel_clone_task

    @rpcsyncwerk_func("int", ["string"])
    def syncwerk_remove_clone_task(repo_id):
        pass
    remove_clone_task = syncwerk_remove_clone_task

    @rpcsyncwerk_func("objlist", [])
    def syncwerk_get_clone_tasks():
        pass
    get_clone_tasks = syncwerk_get_clone_tasks

    @rpcsyncwerk_func("object", ["string"])
    def syncwerk_find_transfer_task(repo_id):
        pass
    find_transfer_task = syncwerk_find_transfer_task

    @rpcsyncwerk_func("object", ["string"])
    def syncwerk_get_checkout_task(repo_id):
        pass
    get_checkout_task = syncwerk_get_checkout_task

    ### sync
    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_sync(repo_id, peer_id):
        pass
    sync = syncwerk_sync

    @rpcsyncwerk_func("object", ["string"])
    def syncwerk_get_repo_sync_task():
        pass
    get_repo_sync_task = syncwerk_get_repo_sync_task

    @rpcsyncwerk_func("object", ["string"])
    def syncwerk_get_repo_sync_info():
        pass
    get_repo_sync_info = syncwerk_get_repo_sync_info

    @rpcsyncwerk_func("int", [])
    def syncwerk_is_auto_sync_enabled():
        pass
    is_auto_sync_enabled = syncwerk_is_auto_sync_enabled

    ###### Property Management #########

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_set_repo_passwd(repo_id, passwd):
        pass
    set_repo_passwd = syncwerk_set_repo_passwd

    @rpcsyncwerk_func("int", ["string", "string", "string"])
    def syncwerk_set_repo_property(repo_id, key, value):
        pass
    set_repo_property = syncwerk_set_repo_property

    @rpcsyncwerk_func("string", ["string", "string"])
    def syncwerk_get_repo_property(repo_id, key):
        pass
    get_repo_property = syncwerk_get_repo_property

    @rpcsyncwerk_func("string", ["string"])
    def syncwerk_get_repo_relay_address(repo_id):
        pass
    get_repo_relay_address = syncwerk_get_repo_relay_address

    @rpcsyncwerk_func("string", ["string"])
    def syncwerk_get_repo_relay_port(repo_id):
        pass
    get_repo_relay_port = syncwerk_get_repo_relay_port

    @rpcsyncwerk_func("int", ["string", "string", "string"])
    def syncwerk_update_repo_relay_info(repo_id, addr, port):
        pass
    update_repo_relay_info = syncwerk_update_repo_relay_info

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_set_repo_token(repo_id, token):
        pass
    set_repo_token = syncwerk_set_repo_token

    @rpcsyncwerk_func("string", ["string"])
    def syncwerk_get_repo_token(repo_id):
        pass
    get_repo_token = syncwerk_get_repo_token

    @rpcsyncwerk_func("object", ["int", "string", "string"])
    def syncwerk_generate_magic_and_random_key(enc_version, repo_id, password):
        pass
    generate_magic_and_random_key = syncwerk_generate_magic_and_random_key

class SyncwerkThreadedRpcClient(ccnet.RpcClientBase):
    """RPC used in client that run in a thread"""

    def __init__(self, ccnet_client_pool, *args, **kwargs):
        ccnet.RpcClientBase.__init__(self, ccnet_client_pool,
                                     "syncwerk-threaded-rpcserver",
                                     *args, **kwargs)

    @rpcsyncwerk_func("int", ["string", "string", "string"])
    def syncwerk_edit_repo():
        pass
    edit_repo = syncwerk_edit_repo

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_reset(repo_id, commit_id):
        pass
    reset = syncwerk_reset

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_revert(repo_id, commit_id):
        pass
    revert = syncwerk_revert

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_add(repo_id, path):
        pass
    add = syncwerk_add

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_rm():
        pass
    rm = syncwerk_rm

    @rpcsyncwerk_func("string", ["string", "string"])
    def syncwerk_commit(repo_id, description):
        pass
    commit = syncwerk_commit


class MonitorRpcClient(ccnet.RpcClientBase):

    def __init__(self, ccnet_client_pool):
        ccnet.RpcClientBase.__init__(self, ccnet_client_pool, "monitor-rpcserver")

    @rpcsyncwerk_func("int", ["string"])
    def monitor_get_repos_size(repo_ids):
        pass
    get_repos_size = monitor_get_repos_size


class SyncwServerRpcClient(ccnet.RpcClientBase):

    def __init__(self, ccnet_client_pool, *args, **kwargs):
        ccnet.RpcClientBase.__init__(self, ccnet_client_pool, "syncwserv-rpcserver",
                                     *args, **kwargs)

    # token for web access to repo
    @rpcsyncwerk_func("string", ["string", "string", "string", "string", "int"])
    def syncwerk_web_get_access_token(repo_id, obj_id, op, username, use_onetime=1):
        pass
    web_get_access_token = syncwerk_web_get_access_token

    @rpcsyncwerk_func("object", ["string"])
    def syncwerk_web_query_access_token(token):
        pass
    web_query_access_token = syncwerk_web_query_access_token

    @rpcsyncwerk_func("string", ["string"])
    def syncwerk_query_zip_progress(token):
        pass
    query_zip_progress = syncwerk_query_zip_progress

    @rpcsyncwerk_func("int", ["string"])
    def cancel_zip_task(token):
        pass

    ###### GC    ####################
    @rpcsyncwerk_func("int", [])
    def syncwerk_gc():
        pass
    gc = syncwerk_gc

    @rpcsyncwerk_func("int", [])
    def syncwerk_gc_get_progress():
        pass
    gc_get_progress = syncwerk_gc_get_progress

    # password management
    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_is_passwd_set(repo_id, user):
        pass
    is_passwd_set = syncwerk_is_passwd_set

    @rpcsyncwerk_func("object", ["string", "string"])
    def syncwerk_get_decrypt_key(repo_id, user):
        pass
    get_decrypt_key = syncwerk_get_decrypt_key

    # Copy tasks

    @rpcsyncwerk_func("object", ["string"])
    def get_copy_task(task_id):
        pass

    @rpcsyncwerk_func("int", ["string"])
    def cancel_copy_task(task_id):
        pass

class SyncwServerThreadedRpcClient(ccnet.RpcClientBase):

    def __init__(self, ccnet_client_pool, *args, **kwargs):
        ccnet.RpcClientBase.__init__(self, ccnet_client_pool,
                                     "syncwserv-threaded-rpcserver",
                                     *args, **kwargs)

    # repo manipulation
    @rpcsyncwerk_func("string", ["string", "string", "string", "string"])
    def syncwerk_create_repo(name, desc, owner_email, passwd):
        pass
    create_repo = syncwerk_create_repo

    @rpcsyncwerk_func("string", ["string", "string", "string", "string", "string", "string", "int"])
    def syncwerk_create_enc_repo(repo_id, name, desc, owner_email, magic, random_key, enc_version):
        pass
    create_enc_repo = syncwerk_create_enc_repo

    @rpcsyncwerk_func("object", ["string"])
    def syncwerk_get_repo(repo_id):
        pass
    get_repo = syncwerk_get_repo

    @rpcsyncwerk_func("int", ["string"])
    def syncwerk_destroy_repo(repo_id):
        pass
    remove_repo = syncwerk_destroy_repo

    @rpcsyncwerk_func("objlist", ["int", "int"])
    def syncwerk_get_repo_list(start, limit):
        pass
    get_repo_list = syncwerk_get_repo_list

    @rpcsyncwerk_func("int64", [])
    def syncwerk_count_repos():
        pass
    count_repos = syncwerk_count_repos

    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def syncwerk_edit_repo(repo_id, name, description, user):
        pass
    edit_repo = syncwerk_edit_repo

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_is_repo_owner(user_id, repo_id):
        pass
    is_repo_owner = syncwerk_is_repo_owner

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_set_repo_owner(email, repo_id):
        pass
    set_repo_owner = syncwerk_set_repo_owner

    @rpcsyncwerk_func("string", ["string"])
    def syncwerk_get_repo_owner(repo_id):
        pass
    get_repo_owner = syncwerk_get_repo_owner

    @rpcsyncwerk_func("objlist", [])
    def syncwerk_get_orphan_repo_list():
        pass
    get_orphan_repo_list = syncwerk_get_orphan_repo_list

    @rpcsyncwerk_func("objlist", ["string", "int", "int", "int"])
    def syncwerk_list_owned_repos(user_id, ret_corrupted, start, limit):
        pass
    list_owned_repos = syncwerk_list_owned_repos

    @rpcsyncwerk_func("int64", ["string"])
    def syncwerk_server_repo_size(repo_id):
        pass
    server_repo_size = syncwerk_server_repo_size

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_repo_set_access_property(repo_id, role):
        pass
    repo_set_access_property = syncwerk_repo_set_access_property

    @rpcsyncwerk_func("string", ["string"])
    def syncwerk_repo_query_access_property(repo_id):
        pass
    repo_query_access_property = syncwerk_repo_query_access_property

    @rpcsyncwerk_func("int",  ["string", "string", "string"])
    def syncwerk_revert_on_server(repo_id, commit_id, user_name):
        pass
    revert_on_server = syncwerk_revert_on_server

    @rpcsyncwerk_func("objlist", ["string", "string", "string"])
    def syncwerk_diff():
        pass
    get_diff = syncwerk_diff

    @rpcsyncwerk_func("int", ["string", "string", "string", "string", "string"])
    def syncwerk_post_file(repo_id, tmp_file_path, parent_dir, filename, user):
        pass
    post_file = syncwerk_post_file

    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def syncwerk_post_dir(repo_id, parent_dir, new_dir_name, user):
        pass
    post_dir = syncwerk_post_dir

    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def syncwerk_post_empty_file(repo_id, parent_dir, filename, user):
        pass
    post_empty_file = syncwerk_post_empty_file

    @rpcsyncwerk_func("int", ["string", "string", "string", "string", "string", "string"])
    def syncwerk_put_file(repo_id, tmp_file_path, parent_dir, filename, user, head_id):
        pass
    put_file = syncwerk_put_file

    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def syncwerk_del_file(repo_id, parent_dir, filename, user):
        pass
    del_file = syncwerk_del_file

    @rpcsyncwerk_func("object", ["string", "string", "string", "string", "string", "string", "string", "int", "int"])
    def syncwerk_copy_file(src_repo, src_dir, src_filename, dst_repo, dst_dir, dst_filename, user, need_progress, synchronous):
        pass
    copy_file = syncwerk_copy_file

    @rpcsyncwerk_func("object", ["string", "string", "string", "string", "string", "string", "int", "string", "int", "int"])
    def syncwerk_move_file(src_repo, src_dir, src_filename, dst_repo, dst_dir, dst_filename, replace, user, need_progress, synchronous):
        pass
    move_file = syncwerk_move_file

    @rpcsyncwerk_func("int", ["string", "string", "string", "string", "string"])
    def syncwerk_rename_file(repo_id, parent_dir, oldname, newname, user):
        pass
    rename_file = syncwerk_rename_file

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_is_valid_filename(repo_id, filename):
        pass
    is_valid_filename = syncwerk_is_valid_filename

    @rpcsyncwerk_func("object", ["string", "int", "string"])
    def syncwerk_get_commit(repo_id, version, commit_id):
        pass
    get_commit = syncwerk_get_commit

    @rpcsyncwerk_func("string", ["string", "string", "int", "int"])
    def syncwerk_list_file_blocks(repo_id, file_id, offset, limit):
        pass
    list_file_blocks = syncwerk_list_file_blocks

    @rpcsyncwerk_func("objlist", ["string", "string", "int", "int"])
    def syncwerk_list_dir(repo_id, dir_id, offset, limit):
        pass
    list_dir = syncwerk_list_dir

    @rpcsyncwerk_func("objlist", ["string", "string", "sting", "string", "int", "int"])
    def list_dir_with_perm(repo_id, dir_path, dir_id, user, offset, limit):
        pass

    @rpcsyncwerk_func("int64", ["string", "int", "string"])
    def syncwerk_get_file_size(store_id, version, file_id):
        pass
    get_file_size = syncwerk_get_file_size

    @rpcsyncwerk_func("int64", ["string", "int", "string"])
    def syncwerk_get_dir_size(store_id, version, dir_id):
        pass
    get_dir_size = syncwerk_get_dir_size

    @rpcsyncwerk_func("objlist", ["string", "string", "string"])
    def syncwerk_list_dir_by_path(repo_id, commit_id, path):
        pass
    list_dir_by_path = syncwerk_list_dir_by_path

    @rpcsyncwerk_func("string", ["string", "string", "string"])
    def syncwerk_get_dir_id_by_commit_and_path(repo_id, commit_id, path):
        pass
    get_dir_id_by_commit_and_path = syncwerk_get_dir_id_by_commit_and_path

    @rpcsyncwerk_func("string", ["string", "string"])
    def syncwerk_get_file_id_by_path(repo_id, path):
        pass
    get_file_id_by_path = syncwerk_get_file_id_by_path

    @rpcsyncwerk_func("string", ["string", "string"])
    def syncwerk_get_dir_id_by_path(repo_id, path):
        pass
    get_dir_id_by_path = syncwerk_get_dir_id_by_path

    @rpcsyncwerk_func("string", ["string", "string", "string"])
    def syncwerk_get_file_id_by_commit_and_path(repo_id, commit_id, path):
        pass
    get_file_id_by_commit_and_path = syncwerk_get_file_id_by_commit_and_path

    @rpcsyncwerk_func("object", ["string", "string"])
    def syncwerk_get_dirent_by_path(repo_id, commit_id, path):
        pass
    get_dirent_by_path = syncwerk_get_dirent_by_path

    @rpcsyncwerk_func("objlist", ["string", "string", "string", "int"])
    def syncwerk_list_file_revisions(repo_id, commit_id, path, limit):
        pass
    list_file_revisions = syncwerk_list_file_revisions

    @rpcsyncwerk_func("objlist", ["string", "string"])
    def syncwerk_calc_files_last_modified(repo_id, parent_dir, limit):
        pass
    calc_files_last_modified = syncwerk_calc_files_last_modified

    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def syncwerk_revert_file(repo_id, commit_id, path, user):
        pass
    revert_file = syncwerk_revert_file

    @rpcsyncwerk_func("string", ["string", "string"])
    def syncwerk_check_repo_blocks_missing(repo_id, blklist):
        pass
    check_repo_blocks_missing = syncwerk_check_repo_blocks_missing

    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def syncwerk_revert_dir(repo_id, commit_id, path, user):
        pass
    revert_dir = syncwerk_revert_dir

    @rpcsyncwerk_func("objlist", ["string", "int", "string", "string", "int"])
    def get_deleted(repo_id, show_days, path, scan_stat, limit):
        pass

    # share repo to user
    @rpcsyncwerk_func("string", ["string", "string", "string", "string"])
    def syncwerk_add_share(repo_id, from_email, to_email, permission):
        pass
    add_share = syncwerk_add_share

    @rpcsyncwerk_func("objlist", ["string", "string", "int", "int"])
    def syncwerk_list_share_repos(email, query_col, start, limit):
        pass
    list_share_repos = syncwerk_list_share_repos

    @rpcsyncwerk_func("objlist", ["string", "string"])
    def syncwerk_list_repo_shared_to(from_user, repo_id):
        pass
    list_repo_shared_to = syncwerk_list_repo_shared_to

    @rpcsyncwerk_func("string", ["string", "string", "string", "string", "string", "string"])
    def share_subdir_to_user(repo_id, path, owner, share_user, permission, passwd):
        pass

    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def unshare_subdir_for_user(repo_id, path, owner, share_user):
        pass

    @rpcsyncwerk_func("int", ["string", "string", "string", "string", "string"])
    def update_share_subdir_perm_for_user(repo_id, path, owner, share_user, permission):
        pass

    @rpcsyncwerk_func("object", ["string", "string", "string", "int"])
    def get_shared_repo_by_path(repo_id, path, shared_to, is_org):
        pass

    @rpcsyncwerk_func("objlist", ["int", "string", "string", "int", "int"])
    def syncwerk_list_org_share_repos(org_id, email, query_col, start, limit):
        pass
    list_org_share_repos = syncwerk_list_org_share_repos

    @rpcsyncwerk_func("int", ["string", "string", "string"])
    def syncwerk_remove_share(repo_id, from_email, to_email):
        pass
    remove_share = syncwerk_remove_share

    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def set_share_permission(repo_id, from_email, to_email, permission):
        pass

    # share repo to group
    @rpcsyncwerk_func("int", ["string", "int", "string", "string"])
    def syncwerk_group_share_repo(repo_id, group_id, user_name, permisson):
        pass
    group_share_repo = syncwerk_group_share_repo

    @rpcsyncwerk_func("int", ["string", "int", "string"])
    def syncwerk_group_unshare_repo(repo_id, group_id, user_name):
        pass
    group_unshare_repo = syncwerk_group_unshare_repo

    @rpcsyncwerk_func("string", ["string"])
    def syncwerk_get_shared_groups_by_repo(repo_id):
        pass
    get_shared_groups_by_repo=syncwerk_get_shared_groups_by_repo

    @rpcsyncwerk_func("objlist", ["string", "string"])
    def syncwerk_list_repo_shared_group(from_user, repo_id):
        pass
    list_repo_shared_group = syncwerk_list_repo_shared_group

    @rpcsyncwerk_func("object", ["string", "string", "int", "int"])
    def get_group_shared_repo_by_path(repo_id, path, group_id, is_org):
        pass

    @rpcsyncwerk_func("objlist", ["string"])
    def get_group_repos_by_user (user):
        pass

    @rpcsyncwerk_func("objlist", ["string", "int"])
    def get_org_group_repos_by_user (user, org_id):
        pass

    @rpcsyncwerk_func("objlist", ["string", "string", "string"])
    def syncwerk_get_shared_users_for_subdir(repo_id, path, from_user):
        pass
    get_shared_users_for_subdir = syncwerk_get_shared_users_for_subdir

    @rpcsyncwerk_func("objlist", ["string", "string", "string"])
    def syncwerk_get_shared_groups_for_subdir(repo_id, path, from_user):
        pass
    get_shared_groups_for_subdir = syncwerk_get_shared_groups_for_subdir

    @rpcsyncwerk_func("string", ["string", "string", "string", "int", "string", "string"])
    def share_subdir_to_group(repo_id, path, owner, share_group, permission, passwd):
        pass

    @rpcsyncwerk_func("int", ["string", "string", "string", "int"])
    def unshare_subdir_for_group(repo_id, path, owner, share_group):
        pass

    @rpcsyncwerk_func("int", ["string", "string", "string", "int", "string"])
    def update_share_subdir_perm_for_group(repo_id, path, owner, share_group, permission):
        pass

    @rpcsyncwerk_func("string", ["int"])
    def syncwerk_get_group_repoids(group_id):
        pass
    get_group_repoids = syncwerk_get_group_repoids

    @rpcsyncwerk_func("objlist", ["int"])
    def syncwerk_get_repos_by_group(group_id):
        pass
    get_repos_by_group = syncwerk_get_repos_by_group

    @rpcsyncwerk_func("objlist", ["string"])
    def get_group_repos_by_owner(user_name):
        pass

    @rpcsyncwerk_func("string", ["string"])
    def get_group_repo_owner(repo_id):
        pass

    @rpcsyncwerk_func("int", ["int", "string"])
    def syncwerk_remove_repo_group(group_id, user_name):
        pass
    remove_repo_group = syncwerk_remove_repo_group

    @rpcsyncwerk_func("int", ["int", "string", "string"])
    def set_group_repo_permission(group_id, repo_id, permission):
        pass

    # branch and commit
    @rpcsyncwerk_func("objlist", ["string"])
    def syncwerk_branch_gets(repo_id):
        pass
    branch_gets = syncwerk_branch_gets

    @rpcsyncwerk_func("objlist", ["string", "int", "int"])
    def syncwerk_get_commit_list(repo_id, offset, limit):
        pass
    get_commit_list = syncwerk_get_commit_list


    ###### Token ####################

    @rpcsyncwerk_func("int", ["string", "string", "string"])
    def syncwerk_set_repo_token(repo_id, email, token):
        pass
    set_repo_token = syncwerk_set_repo_token

    @rpcsyncwerk_func("string", ["string", "string"])
    def syncwerk_get_repo_token_nonnull(repo_id, email):
        """Get the token of the repo for the email user. If the token does not
        exist, a new one is generated and returned.

        """
        pass
    get_repo_token_nonnull = syncwerk_get_repo_token_nonnull


    @rpcsyncwerk_func("string", ["string", "string"])
    def syncwerk_generate_repo_token(repo_id, email):
        pass
    generate_repo_token = syncwerk_generate_repo_token

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_delete_repo_token(repo_id, token, user):
        pass
    delete_repo_token = syncwerk_delete_repo_token

    @rpcsyncwerk_func("objlist", ["string"])
    def syncwerk_list_repo_tokens(repo_id):
        pass
    list_repo_tokens = syncwerk_list_repo_tokens

    @rpcsyncwerk_func("objlist", ["string"])
    def syncwerk_list_repo_tokens_by_email(email):
        pass
    list_repo_tokens_by_email = syncwerk_list_repo_tokens_by_email

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_delete_repo_tokens_by_peer_id(email, user_id):
        pass
    delete_repo_tokens_by_peer_id = syncwerk_delete_repo_tokens_by_peer_id

    @rpcsyncwerk_func("int", ["string"])
    def delete_repo_tokens_by_email(email):
        pass

    ###### quota ##########
    @rpcsyncwerk_func("int64", ["string"])
    def syncwerk_get_user_quota_usage(user_id):
        pass
    get_user_quota_usage = syncwerk_get_user_quota_usage

    @rpcsyncwerk_func("int64", ["string"])
    def syncwerk_get_user_share_usage(user_id):
        pass
    get_user_share_usage = syncwerk_get_user_share_usage

    @rpcsyncwerk_func("int64", ["int"])
    def syncwerk_get_org_quota_usage(org_id):
        pass
    get_org_quota_usage = syncwerk_get_org_quota_usage

    @rpcsyncwerk_func("int64", ["int", "string"])
    def syncwerk_get_org_user_quota_usage(org_id, user):
        pass
    get_org_user_quota_usage = syncwerk_get_org_user_quota_usage

    @rpcsyncwerk_func("int", ["string", "int64"])
    def set_user_quota(user, quota):
        pass

    @rpcsyncwerk_func("int64", ["string"])
    def get_user_quota(user):
        pass

    @rpcsyncwerk_func("int", ["int", "int64"])
    def set_org_quota(org_id, quota):
        pass

    @rpcsyncwerk_func("int64", ["int"])
    def get_org_quota(org_id):
        pass

    @rpcsyncwerk_func("int", ["int", "string", "int64"])
    def set_org_user_quota(org_id, user, quota):
        pass

    @rpcsyncwerk_func("int64", ["int", "string"])
    def get_org_user_quota(org_id, user):
        pass

    @rpcsyncwerk_func("int", ["string", "int64"])
    def check_quota(repo_id, delta):
        pass

    # password management
    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_check_passwd(repo_id, magic):
        pass
    check_passwd = syncwerk_check_passwd

    @rpcsyncwerk_func("int", ["string", "string", "string"])
    def syncwerk_set_passwd(repo_id, user, passwd):
        pass
    set_passwd = syncwerk_set_passwd

    @rpcsyncwerk_func("int", ["string", "string"])
    def syncwerk_unset_passwd(repo_id, user, passwd):
        pass
    unset_passwd = syncwerk_unset_passwd

    # repo permission checking
    @rpcsyncwerk_func("string", ["string", "string"])
    def check_permission(repo_id, user):
        pass

    # folder permission check
    @rpcsyncwerk_func("string", ["string", "string", "string"])
    def check_permission_by_path(repo_id, path, user):
        pass

    # org repo
    @rpcsyncwerk_func("string", ["string", "string", "string", "string", "string", "int", "int"])
    def syncwerk_create_org_repo(name, desc, user, passwd, magic, random_key, enc_version, org_id):
        pass
    create_org_repo = syncwerk_create_org_repo

    @rpcsyncwerk_func("int", ["string"])
    def syncwerk_get_org_id_by_repo_id(repo_id):
        pass
    get_org_id_by_repo_id = syncwerk_get_org_id_by_repo_id

    @rpcsyncwerk_func("objlist", ["int", "int", "int"])
    def syncwerk_get_org_repo_list(org_id, start, limit):
        pass
    get_org_repo_list = syncwerk_get_org_repo_list

    @rpcsyncwerk_func("int", ["int"])
    def syncwerk_remove_org_repo_by_org_id(org_id):
        pass
    remove_org_repo_by_org_id = syncwerk_remove_org_repo_by_org_id

    @rpcsyncwerk_func("objlist", ["int", "string"])
    def list_org_repos_by_owner(org_id, user):
        pass

    @rpcsyncwerk_func("string", ["string"])
    def get_org_repo_owner(repo_id):
        pass

    # org group repo
    @rpcsyncwerk_func("int", ["string", "int", "int", "string", "string"])
    def add_org_group_repo(repo_id, org_id, group_id, owner, permission):
        pass

    @rpcsyncwerk_func("int", ["string", "int", "int"])
    def del_org_group_repo(repo_id, org_id, group_id):
        pass

    @rpcsyncwerk_func("string", ["int", "int"])
    def get_org_group_repoids(org_id, group_id):
        pass

    @rpcsyncwerk_func("string", ["int", "int", "string"])
    def get_org_group_repo_owner(org_id, group_id, repo_id):
        pass

    @rpcsyncwerk_func("objlist", ["int", "string"])
    def get_org_group_repos_by_owner(org_id, user):
        pass

    @rpcsyncwerk_func("string", ["int", "string"])
    def get_org_groups_by_repo(org_id, repo_id):
        pass

    @rpcsyncwerk_func("int", ["int", "int", "string", "string"])
    def set_org_group_repo_permission(org_id, group_id, repo_id, permission):
        pass

    # inner pub repo
    @rpcsyncwerk_func("int", ["string", "string"])
    def set_inner_pub_repo(repo_id, permission):
        pass

    @rpcsyncwerk_func("int", ["string"])
    def unset_inner_pub_repo(repo_id):
        pass

    @rpcsyncwerk_func("objlist", [])
    def list_inner_pub_repos():
        pass

    @rpcsyncwerk_func("objlist", ["string"])
    def list_inner_pub_repos_by_owner(user):
        pass

    @rpcsyncwerk_func("int64", [])
    def count_inner_pub_repos():
        pass

    @rpcsyncwerk_func("int", ["string"])
    def is_inner_pub_repo(repo_id):
        pass

    # org inner pub repo
    @rpcsyncwerk_func("int", ["int", "string", "string"])
    def set_org_inner_pub_repo(org_id, repo_id, permission):
        pass

    @rpcsyncwerk_func("int", ["int", "string"])
    def unset_org_inner_pub_repo(org_id, repo_id):
        pass

    @rpcsyncwerk_func("objlist", ["int"])
    def list_org_inner_pub_repos(org_id):
        pass

    @rpcsyncwerk_func("objlist", ["int", "string"])
    def list_org_inner_pub_repos_by_owner(org_id, user):
        pass

    @rpcsyncwerk_func("int", ["string", "int"])
    def set_repo_history_limit(repo_id, days):
        pass

    @rpcsyncwerk_func("int", ["string"])
    def get_repo_history_limit(repo_id):
        pass

    # virtual repo
    @rpcsyncwerk_func("string", ["string", "string", "string", "string", "string", "string"])
    def create_virtual_repo(origin_repo_id, path, repo_name, repo_desc, owner, passwd=''):
        pass

    @rpcsyncwerk_func("objlist", ["string"])
    def get_virtual_repos_by_owner(owner):
        pass

    @rpcsyncwerk_func("object", ["string", "string", "string"])
    def get_virtual_repo(origin_repo, path, owner):
        pass

    # system default library
    @rpcsyncwerk_func("string", [])
    def get_system_default_repo_id():
        pass

    # Change password
    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def syncwerk_change_repo_passwd(repo_id, old_passwd, new_passwd, user):
        pass
    change_repo_passwd = syncwerk_change_repo_passwd

    # Clean trash
    @rpcsyncwerk_func("int", ["string", "int"])
    def clean_up_repo_history(repo_id, keep_days):
        pass

    # Trashed repos
    @rpcsyncwerk_func("objlist", ["int", "int"])
    def get_trash_repo_list(start, limit):
        pass

    @rpcsyncwerk_func("int", ["string"])
    def del_repo_from_trash(repo_id):
        pass

    @rpcsyncwerk_func("int", ["string"])
    def restore_repo_from_trash(repo_id):
        pass

    @rpcsyncwerk_func("objlist", ["string"])
    def get_trash_repos_by_owner(owner):
        pass

    @rpcsyncwerk_func("int", [])
    def empty_repo_trash():
        pass

    @rpcsyncwerk_func("int", ["string"])
    def empty_repo_trash_by_owner(owner):
        pass

    @rpcsyncwerk_func("object", ["string"])
    def empty_repo_trash_by_owner(owner):
        pass

    @rpcsyncwerk_func("object", ["int", "string", "string"])
    def generate_magic_and_random_key(enc_version, repo_id, password):
        pass

    @rpcsyncwerk_func("int64", [])
    def get_total_file_number():
        pass

    @rpcsyncwerk_func("int64", [])
    def get_total_storage():
        pass

    @rpcsyncwerk_func("object", ["string", "string"])
    def get_file_count_info_by_path(repo_id, path):
        pass

    @rpcsyncwerk_func("string", ["string"])
    def get_trash_repo_owner(repo_id):
        pass

    @rpcsyncwerk_func("int", ["string", "string", "string", "string"])
    def syncwerk_mkdir_with_parents (repo_id, parent_dir, relative_path, username):
        pass
    mkdir_with_parents = syncwerk_mkdir_with_parents

    @rpcsyncwerk_func("int", ["string", "string"])
    def get_server_config_int (group, key):
        pass

    @rpcsyncwerk_func("int", ["string", "string", "int"])
    def set_server_config_int (group, key, value):
        pass

    @rpcsyncwerk_func("int64", ["string", "string"])
    def get_server_config_int64 (group, key):
        pass

    @rpcsyncwerk_func("int", ["string", "string", "int64"])
    def set_server_config_int64 (group, key, value):
        pass

    @rpcsyncwerk_func("string", ["string", "string"])
    def get_server_config_string (group, key):
        pass

    @rpcsyncwerk_func("int", ["string", "string", "string"])
    def set_server_config_string (group, key, value):
        pass

    @rpcsyncwerk_func("int", ["string", "string"])
    def get_server_config_boolean (group, key):
        pass

    @rpcsyncwerk_func("int", ["string", "string", "int"])
    def set_server_config_boolean (group, key, value):
        pass

    @rpcsyncwerk_func("int", ["string", "int"])
    def repo_has_been_shared (repo_id, including_groups):
        pass

    @rpcsyncwerk_func("objlist", ["string"])
    def get_shared_users_by_repo (repo_id):
        pass

    @rpcsyncwerk_func("objlist", ["int", "string"])
    def org_get_shared_users_by_repo (org_id, repo_id):
        pass

    @rpcsyncwerk_func("string", ["string", "string", "string", "int"])
    def convert_repo_path(repo_id, path, user, is_org):
        pass
