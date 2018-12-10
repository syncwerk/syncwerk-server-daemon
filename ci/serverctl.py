#!/usr/bin/env python
#coding: UTF-8

import argparse
import glob
import logging
import os
import re
import sys
from collections import namedtuple
from contextlib import contextmanager
from os.path import abspath, basename, dirname, exists, join

import requests
from tenacity import TryAgain, retry, stop_after_attempt, wait_fixed

from utils import (
    cd, chdir, debug, green, info, mkdirs, red, setup_logging, shell, warning
)

logger = logging.getLogger(__name__)


class ServerCtl(object):
    def __init__(self, datadir, db='sqlite3', syncwerk_server_daemon_bin='syncwerk-server-daemon', syncwerk_server_ccnet_bin='syncwerk-server-ccnet'):
        self.db = db
        self.datadir = datadir
        self.central_conf_dir = join(datadir, 'conf')
        self.syncwerk_conf_dir = join(datadir, 'syncwerk-data')
        self.ccnet_conf_dir = join(datadir, 'ccnet')

        self.log_dir = join(datadir, 'logs')
        mkdirs(self.log_dir)
        self.ccnet_log = join(self.log_dir, 'ccnet.log')
        self.syncwerk_log = join(self.log_dir, 'syncwerk.log')

        self.syncwerk_server_ccnet_bin = syncwerk_server_ccnet_bin
        self.syncwerk_server_daemon_bin = syncwerk_server_daemon_bin

        self.ccnet_proc = None
        self.syncwerk_proc = None

    def setup(self):
        if self.db == 'mysql':
            create_mysql_dbs()

        self.init_ccnet()
        self.init_syncwerk()

    def init_ccnet(self):
        cmd = [
            'ccnet-init',
            '-F',
            self.central_conf_dir,
            '-c',
            self.ccnet_conf_dir,
            '--name',
            'test',
            '--host',
            'test.syncwerk.com',
        ]
        shell(cmd)
        if self.db == 'mysql':
            self.add_ccnet_db_conf()

    def add_ccnet_db_conf(self):
        ccnet_conf = join(self.central_conf_dir, 'ccnet.conf')
        ccnet_db_conf = '''\
[Database]
ENGINE = mysql
HOST = 127.0.0.1
PORT = 3306
USER = syncwerk
PASSWD = syncwerk
DB = ccnet
CONNECTION_CHARSET = utf8
'''
        with open(ccnet_conf, 'a+') as fp:
            fp.write('\n')
            fp.write(ccnet_db_conf)

    def init_syncwerk(self):
        cmd = [
            'syncwerk-server-daemon-init',
            '--central-config-dir',
            self.central_conf_dir,
            '--syncwerk-dir',
            self.syncwerk_conf_dir,
            '--fileserver-port',
            '8082',
        ]

        shell(cmd)
        if self.db == 'mysql':
            self.add_syncwerk_db_conf()

    def add_syncwerk_db_conf(self):
        syncwerk_conf = join(self.central_conf_dir, 'server.conf')
        syncwerk_db_conf = '''\
[database]
type = mysql
host = 127.0.0.1
port = 3306
user = syncwerk
password = syncwerk
db_name = syncwerk
connection_charset = utf8
'''
        with open(syncwerk_conf, 'a+') as fp:
            fp.write('\n')
            fp.write(syncwerk_db_conf)

    @contextmanager
    def run(self):
        try:
            self.start()
            yield self
        except:
            self.print_logs()
            raise
        finally:
            self.stop()

    def print_logs(self):
        for logfile in self.ccnet_log, self.syncwerk_log:
            if exists(logfile):
                shell('cat {0}'.format(logfile))

    @retry(wait=wait_fixed(1), stop=stop_after_attempt(10))
    def wait_ccnet_ready(self):
        if not exists(join(self.ccnet_conf_dir, 'ccnet.sock')):
            raise TryAgain

    def start(self):
        logger.info('Starting ccnet server')
        self.start_ccnet()
        self.wait_ccnet_ready()
        logger.info('Starting syncwerk server')
        self.start_syncwerk()

    def start_ccnet(self):
        cmd = [
            self.syncwerk_server_ccnet_bin,
            "-F",
            self.central_conf_dir,
            "-c",
            self.ccnet_conf_dir,
            "-f",
            self.ccnet_log,
        ]
        self.ccnet_proc = shell(cmd, wait=False)

    def start_syncwerk(self):
        cmd = [
            self.syncwerk_server_daemon_bin,
            "-F",
            self.central_conf_dir,
            "-c",
            self.ccnet_conf_dir,
            "-d",
            self.syncwerk_conf_dir,
            "-l",
            self.syncwerk_log,
        ]
        self.syncwerk_proc = shell(cmd, wait=False)

    def stop(self):
        if self.ccnet_proc:
            logger.info('Stopping ccnet server')
            self.ccnet_proc.terminate()
        if self.syncwerk_proc:
            logger.info('Stopping syncwerk server')
            self.syncwerk_proc.terminate()

    def get_synserv_envs(self):
        envs = dict(os.environ)
        envs.update({
            'SYNCWERK_CENTRAL_CONF_DIR': self.central_conf_dir,
            'CCNET_CONF_DIR': self.ccnet_conf_dir,
            'SYNCWERK_CONF_DIR': self.syncwerk_conf_dir,
        })
        return envs


def create_mysql_dbs():
    sql = '''\
create database `ccnet` character set = 'utf8';
create database `syncwerk` character set = 'utf8';

create user 'syncwerk'@'localhost' identified by 'syncwerk';

GRANT ALL PRIVILEGES ON `ccnet`.* to `syncwerk`@localhost;
GRANT ALL PRIVILEGES ON `syncwerk`.* to `syncwerk`@localhost;
    '''

    shell('mysql -u root', inputdata=sql)
