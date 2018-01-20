#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import errno
import os
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
    args = parser.parse_args()

    if len(args.domains) != 1 and args.domains[0] != 'c-pp':
        print('Only the "c-pp" domain is supported at the moment.')
        return 1

    # TODO(pylaligand): lay out a nice little SDK in the output directory.
    dummy_path = os.path.join(args.out_dir, 'nothing_to_see_yet.txt')
    make_dir(dummy_path)
    with open(dummy_path, 'w') as dummy_file:
        dummy_file.write('Told you, nothing to see here...\n')

    with open(args.stamp_file, 'w') as stamp_file:
        stamp_file.write('Now go use it\n')


if __name__ == '__main__':
    sys.exit(main())
