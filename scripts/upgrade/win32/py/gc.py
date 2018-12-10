# coding: UTF-8

import os
import sys
import traceback
import ccnet

from upgrade_common import install_path, syncwerk_dir, ccnet_dir, run_argv, ensure_server_not_running, central_config_dir


def call_syncwerk_server_gc():
    args = [
        os.path.join(install_path, 'syncwerk', 'bin', 'syncwerk-server-gc.exe'),
        '-c',
        ccnet_dir,
        '-d',
        syncwerk_dir,
        '-F',
        central_config_dir,
    ]

    print 'Starting gc...\n'
    run_argv(args)


def main():
    try:
        ensure_server_not_running()
        call_syncwerk_server_gc()
    except Exception, e:
        print 'Error:\n', e
    else:
        print '\ndone\n'
    finally:
        print '\nprint ENTER to exit\n'
        raw_input()


if __name__ == '__main__':
    main()
