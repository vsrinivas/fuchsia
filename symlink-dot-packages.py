#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import paths
import stat
import subprocess
import sys


def gn_desc(outdir, path):
    '''Run `gn desc` over the whole tree'''
    return json.loads(subprocess.check_output(
        [os.path.join(paths.FUCHSIA_ROOT, 'buildtools', 'gn'), 'desc',
         outdir, path, '--format=json']))


def main():
    parser = argparse.ArgumentParser(
        description=
        'Symlink generated .packages to source directories to help IDEs')
    parser.add_argument("--arch", "-a",
                        choices=["x86-64", "arm64"],
                        default="x86-64",
                        help="architecture (default: x86-64)")
    parser.add_argument("--debug", "-d",
                        dest="variant",
                        action="store_const",
                        const="debug",
                        default="debug",
                        help="use debug build")
    parser.add_argument("--release", "-r",
                        dest="variant",
                        action="store_const",
                        const="release",
                        help="use release build")
    parser.add_argument("--delete", "-e",
                        action="store_true")
    parser.add_argument("--tree", "-t",
                        help="Add symlinks only to a subtree",
                        default="*")
    args = parser.parse_args()

    build_type = '%s-%s' % (args.variant, args.arch)
    outdir = os.path.join(paths.FUCHSIA_ROOT, 'out', build_type)
    if not os.path.exists(outdir):
        print 'ERROR: %s does not exist' % outdir
        sys.exit(2)
    success = True

    for target, props in gn_desc(outdir, args.tree).items():
        # Only look at scripts that run the Dart or Flutter snapshotters
        if props.get('type') != 'action': continue
        if props.get('script') != '//build/dart/gen_dot_packages.py':
            continue

        packages = os.path.join(paths.FUCHSIA_ROOT, props.get('outputs')[0][2:])

        # work out where the target is actually located
        target_dir = os.path.join(paths.FUCHSIA_ROOT, target.split(':')[0][2:])

        # where should the .packages symlink go
        symlink = os.path.join(target_dir, '.packages')

        if not os.path.exists(packages):
            print 'ERROR:    %s not found.' % packages
            success = False
            continue

        if os.path.islink(symlink):
            if args.delete:
                print 'DELETING: %s' % symlink
                os.unlink(symlink)
            elif os.readlink(symlink) != packages:
                print 'UPDATING: %s' % symlink
                os.unlink(symlink)
                os.symlink(packages, symlink)
            else:
                print 'OK:       %s' % symlink
        elif os.path.exists(symlink):
            print 'IGNORING: %s' % symlink
        else:
            if args.delete:
                print 'OK:       %s' % symlink
            else:
                print 'LINKING:  %s' % symlink
                os.symlink(packages, symlink)
    if not success:
        sys.exit(1)


if __name__ == '__main__':
    main()
