# coding: UTF-8

import os

from upgrade_common import install_path, ccnet_dir, syncwerk_dir, upgrade_db, run_argv

def do_migrate_storage():
    '''use syncwerk-server-migrate to migrate objects from the 2.1 layout to 3.0 layout'''
    args = [
        os.path.join(install_path, 'syncwerk', 'bin', 'syncwerk-server-migrate.exe'),
        '-c', ccnet_dir,
        '-d', syncwerk_dir,
    ]

    print 'Starting migrate your data...\n'
    if run_argv(args) != 0:
        raise Exception('failed to migrate syncwerk data to 3.0 format')

def main():
    try:
        upgrade_db('3.0.0')
        do_migrate_storage()
    except Exception, e:
        print 'Error:\n', e
    else:
        print '\ndone\n'
    finally:
        print '\nprint ENTER to exit\n'
        raw_input()

if __name__ == '__main__':
    main()
