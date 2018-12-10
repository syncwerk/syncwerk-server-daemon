# coding: UTF-8

import os
import sys
import sqlite3
import subprocess
import ccnet
import glob


# Directory layout:
#
# - SyncwerkProgram/
#   - syncwserv.ini
#   - syncwerk-server-1.7.0/
#   - syncwerk-server-1.8.0/
#   - syncwerk-server-1.9.0/
#     - upgrade/
#       - sql/
#         - 1.8.0/
#           - sqlite3
#             - ccnet.sql
#             - syncwerk.sql
#             - restapi.sql
#       - upgrade_1.7_1.8.bat
#       - upgrade_1.8_1.9.bat
#       - py/
#         - upgrade_1.7_1.8.py
#         - upgrade_1.8_1.9.py

pyscript_dir = os.path.dirname(os.path.abspath(__file__))
upgrade_dir = os.path.dirname(pyscript_dir)
sql_dir = os.path.join(upgrade_dir, 'sql')
install_path = os.path.dirname(upgrade_dir)
program_top_dir = os.path.dirname(install_path)

syncwserv_dir = ''
ccnet_dir = ''
syncwerk_dir = ''
central_config_dir = ''

def run_argv(argv, cwd=None, env=None, suppress_stdout=False, suppress_stderr=False):
    '''Run a program and wait it to finish, and return its exit code. The
    standard output of this program is supressed.

    '''
    with open(os.devnull, 'w') as devnull:
        if suppress_stdout:
            stdout = devnull
        else:
            stdout = sys.stdout

        if suppress_stderr:
            stderr = devnull
        else:
            stderr = sys.stderr

        proc = subprocess.Popen(argv,
                                cwd=cwd,
                                stdout=stdout,
                                stderr=stderr,
                                env=env)
        return proc.wait()

def error(message):
    print message
    sys.exit(1)

def read_syncwserv_dir():
    global syncwserv_dir, ccnet_dir, syncwerk_dir, central_config_dir
    syncwserv_ini = os.path.join(program_top_dir, 'syncwserv.ini')
    if not os.path.exists(syncwserv_ini):
        error('%s not found' % syncwserv_ini)

    with open(syncwserv_ini, 'r') as fp:
        syncwserv_dir = fp.read().strip()

    ccnet_dir = os.path.join(syncwserv_dir, 'ccnet')
    syncwerk_dir = os.path.join(syncwserv_dir, 'syncwerk-data')
    central_config_dir = os.path.join(syncwserv_dir, 'conf')

def apply_sqls(db_path, sql_path):
    with open(sql_path, 'r') as fp:
        lines = fp.read().split(';')

    with sqlite3.connect(db_path) as conn:
        for line in lines:
            line = line.strip()
            if not line:
                continue
            else:
                conn.execute(line)

def _get_ccnet_db(ccnet_dir, dbname):
    dbs = (
        'ccnet.db',
        'GroupMgr/groupmgr.db',
        'misc/config.db',
        'OrgMgr/orgmgr.db',
    )
    for db in dbs:
        if os.path.splitext(os.path.basename(db))[0] == dbname:
            return os.path.join(ccnet_dir, db)

def _handle_ccnet_sqls(version):
    for sql_path in glob.glob(os.path.join(sql_dir, version, 'sqlite3', 'ccnet', '*.sql')):
        dbname = os.path.splitext(os.path.basename(sql_path))[0]
        apply_sqls(_get_ccnet_db(ccnet_dir, dbname), sql_path)

def upgrade_db(version):
    ensure_server_not_running()
    print 'upgrading databases ...'
    ccnet_db = os.path.join(ccnet_dir, 'ccnet.db')
    syncwerk_db = os.path.join(syncwerk_dir, 'syncwerk.db')
    restapi_db = os.path.join(syncwserv_dir, 'restapi.db')

    def get_sql(prog):
        ret = os.path.join(sql_dir, version, 'sqlite3', '%s.sql' % prog)
        return ret

    ccnet_sql = get_sql('ccnet')
    syncwerk_sql = get_sql('syncwerk')
    restapi_sql = get_sql('restapi')

    if os.path.exists(ccnet_sql):
        print '    upgrading ccnet databases ...'
        apply_sqls(ccnet_db, ccnet_sql)
    _handle_ccnet_sqls(version)

    if os.path.exists(syncwerk_sql):
        print '    upgrading syncwerk databases ...'
        apply_sqls(syncwerk_db, syncwerk_sql)

    if os.path.exists(restapi_sql):
        print '    upgrading restapi databases ...'
        apply_sqls(restapi_db, restapi_sql)

def get_current_version():
    return os.path.basename(install_path).split('-')[-1]

def ensure_server_not_running():
    if os.path.exists(os.path.join(central_config_dir, 'ccnet.conf')):
        client = ccnet.SyncClient(ccnet_dir,
                                  central_config_dir=central_config_dir)
    else:
        client = ccnet.SyncClient(ccnet_dir)
    try:
        client.connect_daemon()
    except ccnet.NetworkError:
        pass
    else:
        raise Exception('Syncwerk server is running! You must turn it off before running this script!')


read_syncwserv_dir()
