#!/bin/bash

echo ""

SCRIPT=$(readlink -f "$0")
INSTALLPATH=$(dirname "${SCRIPT}")
TOPDIR=$(dirname "${INSTALLPATH}")
default_ccnet_conf_dir=${TOPDIR}/ccnet
default_conf_dir=${TOPDIR}/conf
syncwerk_server_fsck=${INSTALLPATH}/syncwerk/bin/syncwerk-server-fsck

export PATH=${INSTALLPATH}/syncwerk/bin:$PATH
export SYNCWERK_LD_LIBRARY_PATH=${INSTALLPATH}/syncwerk/lib/:${INSTALLPATH}/syncwerk/lib64:${LD_LIBRARY_PATH}

script_name=$0
function usage () {
    echo "usage : "
    echo "$(basename ${script_name}) [-h/--help] [-r/--repair] [-E/--export path_to_export] [repo_id_1 [repo_id_2 ...]]"
    echo ""
}

function validate_ccnet_conf_dir () {
    if [[ ! -d ${default_ccnet_conf_dir} ]]; then
        echo "Error: there is no ccnet config directory."
        echo "Have you run setup-syncwerk.sh before this?"
        echo ""
        exit -1;
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

function run_syncwerk_server_fsck () {
    validate_ccnet_conf_dir;
    read_syncwerk_data_dir;

    echo "Starting syncwerk-server-fsck, please wait ..."
    echo

    LD_LIBRARY_PATH=$SYNCWERK_LD_LIBRARY_PATH ${syncwerk_server_fsck} \
        -c "${default_ccnet_conf_dir}" -d "${syncwerk_data_dir}" \
        -F "${default_conf_dir}" \
        ${syncwerk_server_fsck_opts}

    echo "syncwerk-server-fsck run done"
    echo
}

if [ $# -gt 0 ];
then
    for param in $@;
    do
        if [ ${param} = "-h" -o ${param} = "--help" ];
        then
            usage;
            exit 1;
        fi
    done
fi

syncwerk_server_fsck_opts=$@
run_syncwerk_server_fsck;

echo "Done."
