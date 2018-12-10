#!/bin/bash

### BEGIN INIT INFO
# Provides:          restapi
# Required-Start:    $local_fs $remote_fs $network
# Required-Stop:     $local_fs
# Default-Start:     1 2 3 4 5
# Default-Stop:
# Short-Description: Starts Restapi
# Description:       starts Restapi
### END INIT INFO

echo ""

SCRIPT=$(readlink -f "$0")
INSTALLPATH=$(dirname "${SCRIPT}")
TOPDIR=$(dirname "${INSTALLPATH}")
default_ccnet_conf_dir=${TOPDIR}/ccnet
central_config_dir=${TOPDIR}/conf

manage_py=${INSTALLPATH}/restapi/manage.py
gunicorn_conf=${TOPDIR}/conf/gunicorn.conf
pidfile=${TOPDIR}/pids/restapi.pid
errorlog=${TOPDIR}/logs/gunicorn_error.log
accesslog=${TOPDIR}/logs/gunicorn_access.log
gunicorn_exe=${INSTALLPATH}/restapi/thirdpart/gunicorn

script_name=$0
function usage () {
    echo "Usage: "
    echo
    echo "  $(basename ${script_name}) { start <port> | stop | restart <port> }"
    echo
    echo "To run restapi in fastcgi:"
    echo
    echo "  $(basename ${script_name}) { start-fastcgi <port> | stop | restart-fastcgi <port> }"
    echo
    echo "<port> is optional, and defaults to 8000"
    echo ""
}

# Check args
if [[ $1 != "start" && $1 != "stop" && $1 != "restart" \
    && $1 != "start-fastcgi" && $1 != "restart-fastcgi" && $1 != "clearsessions" && $1 != "python-env" ]]; then
    usage;
    exit 1;
fi

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

function validate_restapi_running () {
    if pgrep -f "${manage_py}" 2>/dev/null 1>&2; then
        echo "Restapi is already running."
        exit 1;
    elif pgrep -f "restapi.wsgi:application" 2>/dev/null 1>&2; then
        echo "Restapi is already running."
        exit 1;
    fi
}

function validate_port () {
    if ! [[ ${port} =~ ^[1-9][0-9]{1,4}$ ]] ; then
        printf "\033[033m${port}\033[m is not a valid port number\n\n"
        usage;
        exit 1
    fi
}

if [[ ($1 == "start" || $1 == "restart" || $1 == "start-fastcgi" || $1 == "restart-fastcgi") \
    && ($# == 2 || $# == 1) ]]; then
    if [[ $# == 2 ]]; then
        port=$2
        validate_port
    else
        port=8000
    fi
elif [[ $1 == "stop" && $# == 1 ]]; then
    dummy=dummy
elif [[ $1 == "clearsessions" && $# == 1 ]]; then
    dummy=dummy
elif [[ $1 == "python-env" ]]; then
    dummy=dummy
else
    usage;
    exit 1
fi

function warning_if_syncwerk_not_running () {
    if ! pgrep -f "syncwerk-controller -c ${default_ccnet_conf_dir}" 2>/dev/null 1>&2; then
        echo
        echo "Warning: syncwerk-controller not running. Have you run \"./syncwerk.sh start\" ?"
        echo
        exit 1
    fi
}

function prepare_restapi_log_dir() {
    logdir=${TOPDIR}/logs
    if ! [[ -d ${logsdir} ]]; then
        if ! mkdir -p "${logdir}"; then
            echo "ERROR: failed to create logs dir \"${logdir}\""
            exit 1
        fi
    fi
    export RESTAPI_LOG_DIR=${logdir}
}

function before_start() {
    prepare_env;
    warning_if_syncwerk_not_running;
    validate_restapi_running;
    prepare_restapi_log_dir;
}

function start_restapi () {
    before_start;
    echo "Starting restapi at port ${port} ..."
    check_init_admin;
    $PYTHON $gunicorn_exe restapi.wsgi:application -c "${gunicorn_conf}" --preload

    # Ensure restapi is started successfully
    sleep 5
    if ! pgrep -f "restapi.wsgi:application" 2>/dev/null 1>&2; then
        printf "\033[33mError:Restapi failed to start.\033[m\n"
        echo "Please try to run \"./restapi.sh start\" again"
        exit 1;
    fi
    echo
    echo "Restapi is started"
    echo
}

function start_restapi_fastcgi () {
    before_start;

    # Returns 127.0.0.1 if SYNCWERK_FASTCGI_HOST is unset or hasn't got any value,
    # otherwise returns value of SYNCWERK_FASTCGI_HOST environment variable
    address=`(test -z "$SYNCWERK_FASTCGI_HOST" && echo "127.0.0.1") || echo $SYNCWERK_FASTCGI_HOST`

    echo "Starting restapi (fastcgi) at ${address}:${port} ..."
    check_init_admin;
    $PYTHON "${manage_py}" runfcgi maxchildren=8 host=$address port=$port pidfile=$pidfile \
        outlog=${accesslog} errlog=${errorlog}

    # Ensure restapi is started successfully
    sleep 5
    if ! pgrep -f "${manage_py}" 1>/dev/null; then
        printf "\033[33mError:Restapi failed to start.\033[m\n"
        exit 1;
    fi
    echo
    echo "Restapi is started"
    echo
}

function prepare_env() {
    check_python_executable;
    validate_ccnet_conf_dir;
    read_syncwerk_data_dir;

    if [[ -z "$LANG" ]]; then
        echo "LANG is not set in ENV, set to en_US.UTF-8"
        export LANG='en_US.UTF-8'
    fi
    if [[ -z "$LC_ALL" ]]; then
        echo "LC_ALL is not set in ENV, set to en_US.UTF-8"
        export LC_ALL='en_US.UTF-8'
    fi

    export CCNET_CONF_DIR=${default_ccnet_conf_dir}
    export SYNCWERK_CONF_DIR=${syncwerk_data_dir}
    export SYNCWERK_CENTRAL_CONF_DIR=${central_config_dir}
    export PYTHONPATH=${INSTALLPATH}/syncwerk/lib/python2.6/site-packages:${INSTALLPATH}/syncwerk/lib64/python2.6/site-packages:${INSTALLPATH}/restapi:${INSTALLPATH}/restapi/thirdpart:$PYTHONPATH
    export PYTHONPATH=${INSTALLPATH}/syncwerk/lib/python2.7/site-packages:${INSTALLPATH}/syncwerk/lib64/python2.7/site-packages:$PYTHONPATH

}

function clear_sessions () {
    prepare_env;

    echo "Start clear expired session records ..."
    $PYTHON "${manage_py}" clearsessions

    echo
    echo "Done"
    echo
}

function stop_restapi () {
    if [[ -f ${pidfile} ]]; then
        pid=$(cat "${pidfile}")
        echo "Stopping restapi ..."
        kill ${pid}
        rm -f ${pidfile}
        return 0
    else
        echo "Restapi is not running"
    fi
}

function check_init_admin() {
    check_init_admin_script=${INSTALLPATH}/check_init_admin.py
    if ! $PYTHON $check_init_admin_script; then
        exit 1
    fi
}

function run_python_env() {
    local pyexec

    prepare_env;

    if which ipython 2>/dev/null; then
        pyexec=ipython
    else
        pyexec=$PYTHON
    fi

    if [[ $# -eq 0 ]]; then
        $pyexec "$@"
    else
        "$@"
    fi
}

case $1 in
    "start" )
        start_restapi;
        ;;
    "start-fastcgi" )
        start_restapi_fastcgi;
        ;;
    "stop" )
        stop_restapi;
        ;;
    "restart" )
        stop_restapi
        sleep 2
        start_restapi
        ;;
    "restart-fastcgi" )
        stop_restapi
        sleep 2
        start_restapi_fastcgi
        ;;
    "python-env")
        shift
        run_python_env "$@"
        ;;
    "clearsessions" )
        clear_sessions
        ;;
esac

echo "Done."
echo ""
