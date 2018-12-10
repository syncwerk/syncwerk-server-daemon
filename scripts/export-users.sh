#!/bin/bash

SCRIPT=$(readlink -f "$0")
INSTALLPATH=$(dirname "${SCRIPT}")
TOPDIR=$(dirname "${INSTALLPATH}")
default_ccnet_conf_dir=${TOPDIR}/ccnet
central_config_dir=${TOPDIR}/conf

function check_python_executable() {
    if [[ "$PYTHON" != "" && -x $PYTHON ]]; then
        return 0
    fi

    if which python2.7 2>/dev/null 1>&2; then
        PYTHON=python2.7
    elif which python27 2>/dev/null 1>&2; then
        PYTHON=python27
    else
        echo
        echo "Can't find a python executable of version 2.7 or above in PATH"
        echo "Install python 2.7+ before continue."
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

check_python_executable;
read_syncwerk_data_dir;

export CCNET_CONF_DIR=${default_ccnet_conf_dir}
export SYNCWERK_CONF_DIR=${syncwerk_data_dir}
export SYNCWERK_CENTRAL_CONF_DIR=${central_config_dir}
export PYTHONPATH=${INSTALLPATH}/syncwerk/lib/python2.6/site-packages:${INSTALLPATH}/syncwerk/lib64/python2.6/site-packages:${INSTALLPATH}/restapi/thirdpart:$PYTHONPATH
export PYTHONPATH=${INSTALLPATH}/syncwerk/lib/python2.7/site-packages:${INSTALLPATH}/syncwerk/lib64/python2.7/site-packages:$PYTHONPATH

manage_py=${INSTALLPATH}/restapi/manage.py
exec "$PYTHON" "$manage_py" export_users
