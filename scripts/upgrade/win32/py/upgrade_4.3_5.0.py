# coding: UTF-8

import shutil
import os
import traceback
from os.path import abspath, basename, exists, dirname, join
from upgrade_common import (install_path, syncwserv_dir, ccnet_dir, syncwerk_dir,
                            upgrade_db, run_argv)


def move_all_conf_to_central_config_dir():
    central_config_dir = join(syncwserv_dir, 'conf')
    if not exists(central_config_dir):
        os.mkdir(central_config_dir)
    files = [
        join(ccnet_dir, 'ccnet.conf'),
        join(syncwerk_dir, 'server.conf'),
        join(syncwserv_dir, 'restapi_settings.py'),
    ]
    for fn in files:
        if not exists(fn):
            raise RuntimeError('file %s does not exist' % fn)
    for fn in files:
        with open(fn, 'r') as fp:
            if 'This file has been moved' in fp.read():
                return
        dstfile = join(central_config_dir, basename(fn))
        shutil.copyfile(fn, dstfile)
        with open(fn, 'w') as fp:
            content = '# This file has been moved to %s in syncwerk 5.0.0' % dstfile
            fp.write(content)


def main():
    try:
        upgrade_db('5.0.0')
        move_all_conf_to_central_config_dir()
    except Exception, e:
        traceback.print_exc()
        print 'Error:\n', e
    else:
        print '\ndone\n'
    finally:
        print '\nprint ENTER to exit\n'
        raw_input()


if __name__ == '__main__':
    main()
