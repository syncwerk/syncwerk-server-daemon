#!/bin/bash

SCRIPT=$(readlink -f "$0") # syncwerk/syncwerk-server-1.3.0/upgrade/upgrade_xx_xx.sh
UPGRADE_DIR=$(dirname "$SCRIPT") # syncwerk/syncwerk-server-1.3.0/upgrade/
INSTALLPATH=$(dirname "$UPGRADE_DIR") # syncwerk/syncwerk-server-1.3.0/
TOPDIR=$(dirname "${INSTALLPATH}") # syncwerk/
default_ccnet_conf_dir=${TOPDIR}/ccnet
default_syncwerk_data_dir=${TOPDIR}/syncwerk-data
default_restapi_db=${TOPDIR}/restapi.db

export CCNET_CONF_DIR=${default_ccnet_conf_dir}
export PYTHONPATH=${INSTALLPATH}/syncwerk/lib/python2.6/site-packages:${INSTALLPATH}/syncwerk/lib64/python2.6/site-packages:${INSTALLPATH}/syncwerk/lib/python2.7/site-packages:${INSTALLPATH}/restapi/thirdpart:$PYTHONPATH
export PYTHONPATH=${INSTALLPATH}/syncwerk/lib/python2.7/site-packages:${INSTALLPATH}/syncwerk/lib64/python2.7/site-packages:$PYTHONPATH

prev_version=1.4
current_version=1.5

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
}

check_python_executable
read_syncwerk_data_dir

export SYNCWERK_CONF_DIR=$syncwerk_data_dir

# test whether syncwerk server has been stopped.
if pgrep syncwerk-server-daemon 2>/dev/null 1>&2 ; then
    echo 
    echo "syncwerk server is still running !"
    echo "stop it using scripts before upgrade."
    echo
    exit 1
elif pgrep -f "manage.py run_gunicorn" 2>/dev/null 1>&2 ; then
    echo 
    echo "restapi server is still running !"
    echo "stop it before upgrade."
    echo
    exit 1
fi

echo
echo "------------------------------"
echo "migrating avatars ..."
echo
media_dir=${INSTALLPATH}/restapi/media
orig_avatar_dir=${INSTALLPATH}/restapi/media/avatars
dest_avatar_dir=${TOPDIR}/restapi-data/avatars

# move "media/avatars" directory outside 
if [[ ! -d ${dest_avatar_dir} ]]; then
    mkdir -p "${TOPDIR}/restapi-data"
    mv "${orig_avatar_dir}" "${dest_avatar_dir}" 2>/dev/null 1>&2
    ln -s ../../../restapi-data/avatars ${media_dir}

elif [[ ! -L ${orig_avatar_dir} ]]; then
    mv ${orig_avatar_dir}/* "${dest_avatar_dir}" 2>/dev/null 1>&2
    rm -rf "${orig_avatar_dir}"
    ln -s ../../../restapi-data/avatars ${media_dir}
fi

echo "DONE"
echo "------------------------------"
echo