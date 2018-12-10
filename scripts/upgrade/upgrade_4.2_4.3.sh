#!/bin/bash

SCRIPT=$(readlink -f "$0") # syncwerk/syncwerk-server-1.3.0/upgrade/upgrade_xx_xx.sh
UPGRADE_DIR=$(dirname "$SCRIPT") # syncwerk/syncwerk-server-1.3.0/upgrade/
INSTALLPATH=$(dirname "$UPGRADE_DIR") # syncwerk/syncwerk-server-1.3.0/
TOPDIR=$(dirname "${INSTALLPATH}") # syncwerk/
default_ccnet_conf_dir=${TOPDIR}/ccnet
default_syncwerk_data_dir=${TOPDIR}/syncwerk-data
default_restapi_db=${TOPDIR}/restapi.db
default_conf_dir=${TOPDIR}/conf
syncwerk_server_symlink=${TOPDIR}/syncwerk-server-latest
restapi_data_dir=${TOPDIR}/restapi-data
restapi_settings_py=${TOPDIR}/restapi_settings.py

manage_py=${INSTALLPATH}/restapi/manage.py

export CCNET_CONF_DIR=${default_ccnet_conf_dir}
export PYTHONPATH=${INSTALLPATH}/syncwerk/lib/python2.6/site-packages:${INSTALLPATH}/syncwerk/lib64/python2.6/site-packages:${INSTALLPATH}/syncwerk/lib/python2.7/site-packages:${INSTALLPATH}/restapi/thirdpart:$PYTHONPATH
export PYTHONPATH=${INSTALLPATH}/syncwerk/lib/python2.7/site-packages:${INSTALLPATH}/syncwerk/lib64/python2.7/site-packages:$PYTHONPATH
export SYNCWERK_LD_LIBRARY_PATH=${INSTALLPATH}/syncwerk/lib/:${INSTALLPATH}/syncwerk/lib64:${LD_LIBRARY_PATH}

prev_version=4.2
current_version=4.3

echo
echo "-------------------------------------------------------------"
echo "This script would upgrade your syncwerk server from ${prev_version} to ${current_version}"
echo "Press [ENTER] to contiune"
echo "-------------------------------------------------------------"
echo
read dummy

function check_python_executable() {
    if [[ "$PYTHON" != "" && -x $PYTHON ]]; then
        return 0
    fi

    if which python2.7 2>/dev/null 1>&2; then
        PYTHON=python2.7
    elif which python27 2>/dev/null 1>&2; then
        PYTHON=python27
    elif which python2.6 2>/dev/null 1>&2; then
        PYTHON=python2.6
    elif which python26 2>/dev/null 1>&2; then
        PYTHON=python26
    else
        echo
        echo "Can't find a python executable of version 2.6 or above in PATH"
        echo "Install python 2.6+ before continue."
        echo "Or if you installed it in a non-standard PATH, set the PYTHON enviroment varirable to it"
        echo
        exit 1
    fi
}

function read_syncwerk_data_dir () {
    syncwerk_ini=${default_ccnet_conf_dir}/storage.ini
    if [[ ! -f ${syncwerk_ini} ]]; then
        echo "${syncwerk_ini} not found. Now quit"
        exit 1
    fi
    syncwerk_data_dir=$(cat "${syncwerk_ini}")
    if [[ ! -d ${syncwerk_data_dir} ]]; then
        echo "Your syncwerk server data directory \"${syncwerk_data_dir}\" is invalid or doesn't exits."
        echo "Please check it first, or create this directory yourself."
        echo ""
        exit 1;
    fi

    export SYNCWERK_CONF_DIR=$syncwerk_data_dir
}

function ensure_server_not_running() {
    # test whether syncwerk server has been stopped.
    if pgrep syncwerk-server-daemon 2>/dev/null 1>&2 ; then
        echo
        echo "syncwerk server is still running !"
        echo "stop it using scripts before upgrade."
        echo
        exit 1
    elif pgrep -f "${manage_py} run_gunicorn" 2>/dev/null 1>&2 ; then
        echo
        echo "restapi server is still running !"
        echo "stop it before upgrade."
        echo
        exit 1
    elif pgrep -f "${manage_py} runfcgi" 2>/dev/null 1>&2 ; then
        echo
        echo "restapi server is still running !"
        echo "stop it before upgrade."
        echo
        exit 1
    fi
}

function migrate_avatars() {
    echo
    echo "migrating avatars ..."
    echo
    media_dir=${INSTALLPATH}/restapi/media
    orig_avatar_dir=${INSTALLPATH}/restapi/media/avatars
    dest_avatar_dir=${TOPDIR}/restapi-data/avatars

    # move "media/avatars" directory outside
    if [[ ! -d ${dest_avatar_dir} ]]; then
        mkdir -p "${TOPDIR}/restapi-data"
        mv "${orig_avatar_dir}" "${dest_avatar_dir}" 2>/dev/null 1>&2
        ln -s ../../../restapi-data/avatars "${media_dir}"

    elif [[ ! -L ${orig_avatar_dir} ]]; then
        mv "${orig_avatar_dir}"/* "${dest_avatar_dir}" 2>/dev/null 1>&2
        rm -rf "${orig_avatar_dir}"
        ln -s ../../../restapi-data/avatars "${media_dir}"
    fi
    echo "Done"
}

function update_database() {
    echo
    echo "Updating syncwerk/restapi database ..."
    echo

    db_update_helper=${UPGRADE_DIR}/db_update_helper.py
    if ! $PYTHON "${db_update_helper}" 4.3.0; then
        echo
        echo "Failed to upgrade your database"
        echo
        exit 1
    fi
    echo "Done"
}

function upgrade_syncwerk_server_latest_symlink() {
    # update the symlink syncwerk-server to the new server version
    if [[ -L "${syncwerk_server_symlink}" || ! -e "${syncwerk_server_symlink}" ]]; then
        echo
        printf "updating \033[33m${syncwerk_server_symlink}\033[m symbolic link to \033[33m${INSTALLPATH}\033[m ...\n\n"
        echo
        if ! rm -f "${syncwerk_server_symlink}"; then
            echo "Failed to remove ${syncwerk_server_symlink}"
            echo
            exit 1;
        fi

        if ! ln -s "$(basename ${INSTALLPATH})" "${syncwerk_server_symlink}"; then
            echo "Failed to update ${syncwerk_server_symlink} symbolic link."
            echo
            exit 1;
        fi
    fi
}

function make_media_custom_symlink() {
    media_symlink=${INSTALLPATH}/restapi/media/custom
    if [[ -L "${media_symlink}" ]]; then
        return

    elif [[ ! -e "${media_symlink}" ]]; then
        ln -s ../../../restapi-data/custom "${media_symlink}"
        return


    elif [[ -d "${media_symlink}" ]]; then
        cp -rf "${media_symlink}" "${restapi_data_dir}/"
        rm -rf "${media_symlink}"
        ln -s ../../../restapi-data/custom "${media_symlink}"
    fi

}

function move_old_customdir_outside() {
    # find the path of the latest syncwerk server folder
    if [[ -L ${syncwerk_server_symlink} ]]; then
        latest_server=$(readlink -f "${syncwerk_server_symlink}")
    else
        return
    fi

    old_customdir=${latest_server}/restapi/media/custom

    # old customdir is already a symlink, do nothing
    if [[ -L "${old_customdir}" ]]; then
        return
    fi

    # old customdir does not exist, do nothing
    if [[ ! -e "${old_customdir}" ]]; then
        return
    fi

    # media/custom exist and is not a symlink
    cp -rf "${old_customdir}" "${restapi_data_dir}/"
}

function regenerate_secret_key() {
    regenerate_secret_key_script=$UPGRADE_DIR/regenerate_secret_key.sh
    if ! $regenerate_secret_key_script ; then
        echo "Failed to regenerate the restapi secret key"
        exit 1
    fi
}

#################
# The main execution flow of the script
################

check_python_executable;
read_syncwerk_data_dir;
ensure_server_not_running;

regenerate_secret_key;

update_database;

migrate_avatars;


move_old_customdir_outside;
make_media_custom_symlink;
upgrade_syncwerk_server_latest_symlink;

echo
echo "-----------------------------------------------------------------"
echo "Upgraded your syncwerk server successfully."
echo "-----------------------------------------------------------------"
echo
