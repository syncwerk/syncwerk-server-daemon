#!/bin/bash

# This is a wrapper shell script for the real syncwerk-server-cli command.
# It prepares necessary environment variables and exec the real script.

# syncwerk cli client requires python 2.6 or 2.7
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

check_python_executable

# syncwerk cli client requires the argparse module
if ! $PYTHON -c 'import argparse' 2>/dev/null 1>&2; then
    echo
    echo "Python argparse module is required"
    echo "see [https://pypi.python.org/pypi/argparse]"
    echo
    exit 1
fi

SCRIPT=$(readlink -f "$0")
INSTALLPATH=$(dirname "${SCRIPT}")

SYNCWERK_BIN_DIR=${INSTALLPATH}/bin
SYNCWERK_LIB_DIR=${INSTALLPATH}/lib:${INSTALLPATH}/lib64
SYNCWERK_PYTHON_PATH=${INSTALLPATH}/lib/python2.6/site-packages:${INSTALLPATH}/lib64/python2.6/site-packages:${INSTALLPATH}/lib/python2.7/site-packages:${INSTALLPATH}/lib64/python2.7/site-packages

SYNCWERK_SERVER_CLI=${SYNCWERK_BIN_DIR}/syncwerk-server-cli.py

PATH=${SYNCWERK_BIN_DIR}:${PATH} \
PYTHONPATH=${SYNCWERK_PYTHON_PATH}:${PYTHONPATH} \
SYNCWERK_LD_LIBRARY_PATH=${SYNCWERK_LIB_DIR}:${LD_LIBRARY_PATH} \
exec $PYTHON ${SYNCWERK_SERVER_CLI} "$@"
