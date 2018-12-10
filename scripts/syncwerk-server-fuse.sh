#!/bin/bash

echo ""

SCRIPT=$(readlink -f "$0")
INSTALLPATH=$(dirname "${SCRIPT}")
TOPDIR=$(dirname "${INSTALLPATH}")
default_ccnet_conf_dir=${TOPDIR}/ccnet
default_conf_dir=${TOPDIR}/conf
syncwerk_server_fuse=${INSTALLPATH}/syncwerk/bin/syncwerk-server-fuse

export PATH=${INSTALLPATH}/syncwerk/bin:$PATH
export SYNCWERK_LD_LIBRARY_PATH=${INSTALLPATH}/syncwerk/lib/:${INSTALLPATH}/syncwerk/lib64:${LD_LIBRARY_PATH}

script_name=$0
function usage () {
    echo "usage : "
    echo "$(basename ${script_name}) { start <mount-point> | stop | restart <mount-point> } "
    echo ""
}

# check args
if [[ "$1" != "start" && "$1" != "stop" && "$1" != "restart" ]]; then
    usage;
    exit 1;
fi

if [[ ($1 == "start" || $1 == "restart" ) && $# -lt 2 ]]; then
    usage;
    exit 1
fi

if [[ $1 == "stop" && $# != 1 ]]; then
    usage;
    exit 1
fi

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

function validate_already_running () {
    if pid=$(pgrep -f "syncwerk-server-fuse -c ${default_ccnet_conf_dir}" 2>/dev/null); then
        echo "syncwerk-server-fuse is already running, pid $pid"
        echo
        exit 1;
    fi
}

function warning_if_syncwerk_not_running () {
    if ! pgrep -f "syncwerk-controller -c ${default_ccnet_conf_dir}" 2>/dev/null 1>&2; then
        echo
        echo "Warning: syncwerk-controller not running. Have you run \"./syncwerk.sh start\" ?"
        echo
    fi
}

function start_syncwerk_server_fuse () {
    validate_already_running;
    warning_if_syncwerk_not_running;
    validate_ccnet_conf_dir;
    read_syncwerk_data_dir;

    echo "Starting syncwerk-server-fuse, please wait ..."

    logfile=${TOPDIR}/logs/syncwerk-server-fuse.log

    LD_LIBRARY_PATH=$SYNCWERK_LD_LIBRARY_PATH ${syncwerk_server_fuse} \
        -c "${default_ccnet_conf_dir}" \
        -d "${syncwerk_data_dir}" \
        -F "${default_conf_dir}" \
        -l "${logfile}" \
        "$@"

    sleep 2

    # check if syncwerk-server-fuse started successfully
    if ! pgrep -f "syncwerk-server-fuse -c ${default_ccnet_conf_dir}" 2>/dev/null 1>&2; then
        echo "Failed to start syncwerk-server-fuse"
        exit 1;
    fi

    echo "syncwerk-server-fuse started"
    echo
}

function stop_syncwerk_server_fuse() {
    if ! pgrep -f "syncwerk-server-fuse -c ${default_ccnet_conf_dir}" 2>/dev/null 1>&2; then
        echo "syncwerk-server-fuse not running yet"
        return 1;
    fi

    echo "Stopping syncwerk-server-fuse ..."
    pkill -SIGTERM -f "syncwerk-server-fuse -c ${default_ccnet_conf_dir}"
    return 0
}

function restart_syncwerk_server_fuse () {
    stop_syncwerk_server_fuse
    sleep 2
    start_syncwerk_server_fuse $@
}

case $1 in
    "start" )
	shift
        start_syncwerk_server_fuse $@;
        ;;
    "stop" )
        stop_syncwerk_server_fuse;
        ;;
    "restart" )
	shift
        restart_syncwerk_server_fuse $@;
esac

echo "Done."
