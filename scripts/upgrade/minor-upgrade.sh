#!/bin/bash

SCRIPT=$(readlink -f "$0") # syncwerk/syncwerk-server-1.3.0/upgrade/upgrade_xx_xx.sh
UPGRADE_DIR=$(dirname "$SCRIPT") # syncwerk/syncwerk-server-1.3.0/upgrade/
INSTALLPATH=$(dirname "$UPGRADE_DIR") # syncwerk/syncwerk-server-1.3.0/
TOPDIR=$(dirname "${INSTALLPATH}") # syncwerk/

echo
echo "-------------------------------------------------------------"
echo "This script would do the minor upgrade for you."
echo "Press [ENTER] to contiune"
echo "-------------------------------------------------------------"
echo
read dummy

media_dir=${INSTALLPATH}/restapi/media
orig_avatar_dir=${INSTALLPATH}/restapi/media/avatars
dest_avatar_dir=${TOPDIR}/restapi-data/avatars
syncwerk_server_symlink=${TOPDIR}/syncwerk-server-latest
restapi_data_dir=${TOPDIR}/restapi-data

function migrate_avatars() {
    echo
    echo "------------------------------"
    echo "migrating avatars ..."
    echo
    # move "media/avatars" directory outside
    if [[ ! -d ${dest_avatar_dir} ]]; then
        echo
        echo "Error: avatars directory \"${dest_avatar_dir}\" does not exist" 2>&1
        echo
        exit 1

    elif [[ ! -L ${orig_avatar_dir} ]]; then
        mv "${orig_avatar_dir}"/* "${dest_avatar_dir}" 2>/dev/null 1>&2
        rm -rf "${orig_avatar_dir}"
        ln -s ../../../restapi-data/avatars "${media_dir}"
    fi
    echo
    echo "DONE"
    echo "------------------------------"
    echo
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

function update_latest_symlink() {
    # update the symlink syncwerk-server to the new server version
    echo
    echo "updating syncwerk-server-latest symbolic link to ${INSTALLPATH} ..."
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
}

migrate_avatars;

move_old_customdir_outside;
make_media_custom_symlink;

update_latest_symlink;


echo "DONE"
echo "------------------------------"
echo
