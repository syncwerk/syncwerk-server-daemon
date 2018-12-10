#!/bin/bash

echo ""

SCRIPT=$(readlink -f "$0")
INSTALLPATH=$(dirname "${SCRIPT}")
TOPDIR=$(dirname "${INSTALLPATH}")
default_ccnet_conf_dir=${TOPDIR}/ccnet
default_conf_dir=${TOPDIR}/conf
syncwerk_server_gc=${INSTALLPATH}/syncwerk/bin/syncwerk-server-gc
syncwerk_server_gc_opts=""

export PATH=${INSTALLPATH}/syncwerk/bin:$PATH
export SYNCWERK_LD_LIBRARY_PATH=${INSTALLPATH}/syncwerk/lib/:${INSTALLPATH}/syncwerk/lib64:${LD_LIBRARY_PATH}

script_name=$0
function usage () {
    echo "usage : "
    echo "$(basename ${script_name}) [--dry-run | -D] [--rm-deleted | -r] [repo-id1] [repo-id2]"
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

function check_component_running() {
    name=$1
    cmd=$2
    if pid=$(pgrep -f "$cmd" 2>/dev/null); then
        echo "[$name] is running, pid $pid. You can stop it by: "
        echo
        echo "        kill $pid"
        echo
        echo "Stop it and try again."
        echo
        exit
    fi
}

function validate_already_running () {
    if pid=$(pgrep -f "syncwerk-controller -c ${default_ccnet_conf_dir}" 2>/dev/null); then
        echo "syncwerk server is still running, stop it by \"syncwerk.sh stop\""
        echo
        exit 1;
    fi

    check_component_running "syncwerk-server-ccnet" "syncwerk-server-ccnet -c ${default_ccnet_conf_dir}"
    check_component_running "syncwerk-server-daemon" "syncwerk-server-daemon -c ${default_ccnet_conf_dir}"
    check_component_running "fileserver" "fileserver -c ${default_ccnet_conf_dir}"
    check_component_running "syncwdav" "wsgidav.server.run_server"
}

function run_syncwerk_server_gc () {
    validate_already_running;
    validate_ccnet_conf_dir;
    read_syncwerk_data_dir;

    echo "Starting syncwerk-server-gc, please wait ..."

    LD_LIBRARY_PATH=$SYNCWERK_LD_LIBRARY_PATH ${syncwerk_server_gc} \
        -c "${default_ccnet_conf_dir}" \
        -d "${syncwerk_data_dir}" \
        -F "${default_conf_dir}" \
        ${syncwerk_server_gc_opts}

    echo "syncwerk-server-gc run done"
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

syncwerk_server_gc_opts=$@
run_syncwerk_server_gc;

echo "Done."
