#!/bin/bash

### BEGIN INIT INFO
# Provides:          syncwerk
# Required-Start:    $local_fs $remote_fs $network
# Required-Stop:     $local_fs
# Default-Start:     1 2 3 4 5
# Default-Stop:
# Short-Description: Starts Syncwerk Server
# Description:       starts Syncwerk Server
### END INIT INFO

echo ""

SCRIPT=$(readlink -f "$0")
INSTALLPATH=$(dirname "${SCRIPT}")
TOPDIR=$(dirname "${INSTALLPATH}")
default_ccnet_conf_dir=${TOPDIR}/ccnet
central_config_dir=${TOPDIR}/conf
syncw_controller="${INSTALLPATH}/syncwerk/bin/syncwerk-controller"


export PATH=${INSTALLPATH}/syncwerk/bin:$PATH
export ORIG_LD_LIBRARY_PATH=${LD_LIBRARY_PATH}
export SYNCWERK_LD_LIBRARY_PATH=${INSTALLPATH}/syncwerk/lib/:${INSTALLPATH}/syncwerk/lib64:${LD_LIBRARY_PATH}

script_name=$0
function usage () {
    echo "usage : "
    echo "$(basename ${script_name}) { start | stop | restart } "
    echo ""
}

# check args
if [[ $# != 1 || ( "$1" != "start" && "$1" != "stop" && "$1" != "restart" ) ]]; then
    usage;
    exit 1;
fi

function validate_running_user () {
    real_data_dir=`readlink -f ${syncwerk_data_dir}`
    running_user=`id -un`
    data_dir_owner=`stat -c %U ${real_data_dir}`

    if [[ "${running_user}" != "${data_dir_owner}" ]]; then
        echo "Error: the user running the script (\"${running_user}\") is not the owner of \"${real_data_dir}\" folder, you should use the user \"${data_dir_owner}\" to run the script."
        exit -1;
    fi
}

function validate_ccnet_conf_dir () {
    if [[ ! -d ${default_ccnet_conf_dir} ]]; then
        echo "Error: there is no ccnet config directory."
        echo "Have you run setup-syncwerk.sh before this?"
        echo ""
        exit -1;
    fi
}

function validate_central_conf_dir () {
    if [[ ! -d ${central_config_dir} ]]; then
        echo "Error: there is no conf/ directory."
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

function test_config() {
    if ! LD_LIBRARY_PATH=$SYNCWERK_LD_LIBRARY_PATH ${syncw_controller} --test \
         -c "${default_ccnet_conf_dir}" \
         -d "${syncwerk_data_dir}" \
         -F "${central_config_dir}" ; then
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
        echo "Syncwerk controller is already running, pid $pid"
        echo
        exit 1;
    fi

    check_component_running "syncwerk-server-ccnet" "syncwerk-server-ccnet -c ${default_ccnet_conf_dir}"
    check_component_running "syncwerk-server-daemon" "syncwerk-server-daemon -c ${default_ccnet_conf_dir}"
    check_component_running "fileserver" "fileserver -c ${default_ccnet_conf_dir}"
    check_component_running "syncwdav" "wsgidav.server.run_server"
}

function start_syncwerk_server () {
    validate_already_running;
    validate_central_conf_dir;
    validate_ccnet_conf_dir;
    read_syncwerk_data_dir;
    validate_running_user;
    test_config;

    echo "Starting syncwerk server, please wait ..."

    mkdir -p $TOPDIR/logs
    LD_LIBRARY_PATH=$SYNCWERK_LD_LIBRARY_PATH ${syncw_controller} \
                   -c "${default_ccnet_conf_dir}" \
                   -d "${syncwerk_data_dir}" \
                   -F "${central_config_dir}"

    sleep 3

    # check if syncwerk server started successfully
    if ! pgrep -f "syncwerk-controller -c ${default_ccnet_conf_dir}" 2>/dev/null 1>&2; then
        echo "Failed to start syncwerk server"
        exit 1;
    fi

    echo "Syncwerk server started"
    echo
}

function stop_syncwerk_server () {
    if ! pgrep -f "syncwerk-controller -c ${default_ccnet_conf_dir}" 2>/dev/null 1>&2; then
        echo "syncwerk server not running yet"
        return 1;
    fi

    echo "Stopping syncwerk server ..."
    pkill -SIGTERM -f "syncwerk-controller -c ${default_ccnet_conf_dir}"
    pkill -f "syncwerk-server-ccnet -c ${default_ccnet_conf_dir}"
    pkill -f "syncwerk-server-daemon -c ${default_ccnet_conf_dir}"
    pkill -f "fileserver -c ${default_ccnet_conf_dir}"
    pkill -f "soffice.*--invisible --nocrashreport"
    pkill -f  "wsgidav.server.run_server"
    return 0
}

function restart_syncwerk_server () {
    stop_syncwerk_server;
    sleep 2
    start_syncwerk_server;
}

case $1 in
    "start" )
        start_syncwerk_server;
        ;;
    "stop" )
        stop_syncwerk_server;
        ;;
    "restart" )
        restart_syncwerk_server;
esac

echo "Done."
