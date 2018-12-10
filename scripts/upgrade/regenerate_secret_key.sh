#!/bin/bash

SCRIPT=$(readlink -f "$0")
UPGRADEDIR=$(dirname "${SCRIPT}")
INSTALLPATH=$(dirname "${UPGRADEDIR}")
TOPDIR=$(dirname "${INSTALLPATH}")

restapi_secret_keygen=${INSTALLPATH}/restapi/tools/secret_key_generator.py
restapi_settings_py=${TOPDIR}/restapi_settings.py

line="SECRET_KEY = \"$(python $restapi_secret_keygen)\""

sed -i -e "/SECRET_KEY/c\\$line" $restapi_settings_py
