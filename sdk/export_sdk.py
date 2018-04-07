#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import errno
import json
import os
import shutil
import sys


def make_dir(path, is_dir=False):
    """Creates the directory at `path`."""
    target = path if is_dir else os.path.dirname(path)
    try:
        os.makedirs(target)
    except OSError as exception:
        if exception.errno == errno.EEXIST and os.path.isdir(target):
            pass
        else:
            raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-dir',
                        help='Path to the directory where to install the SDK',
                        required=True)
    parser.add_argument('--stamp-file',
                        help='Path to the victory file',
                        required=True)
    parser.add_argument('--manifest',
                        help='Path to the SDK\'s manifest file',
                        required=True)
    parser.add_argument('--domains',
                        help='List of domains to export',
                        nargs='*')
    parser.add_argument('--depname',
                        help='Name of the depfile target',
                        required=True)
    parser.add_argument('--depfile',
                        help='Path to the depfile to generate',
                        required=True)
    parser.add_argument('--old-school',
                        help='Turns the SDK into a big sysroot',
                        action='store_true')
    args = parser.parse_args()

    if len(args.domains) != 1 and args.domains[0] != 'cpp':
        print('Only the "cpp" domain is supported at the moment.')
        return 1

    # Remove any existing output.
    shutil.rmtree(args.out_dir, True)

    with open(args.manifest, 'r') as manifest_file:
        manifest = json.load(manifest_file)
    atoms = filter(lambda a: a['id']['domain'] == 'cpp', manifest['atoms'])

    def get_atom_dir(atom_name):
        if args.old_school:
            return os.path.join(args.out_dir, 'sysroot')
        else:
            return os.path.join(args.out_dir, 'pkg', atom_name)

    for atom in atoms:
        dir = get_atom_dir(atom['id']['name'])
        for file in atom['files']:
            destination = os.path.join(dir, file['destination'])
            make_dir(destination)
            shutil.copyfile(file['source'], destination)

    with open(args.depfile, 'w') as dep_file:
        dep_file.write('%s:\n' % args.depname)

    with open(args.stamp_file, 'w') as stamp_file:
        stamp_file.write('Now go use it\n')


if __name__ == '__main__':
    sys.exit(main())
