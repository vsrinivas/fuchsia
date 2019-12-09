#!/usr/bin/env python2.7
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
    parser.add_argument(
        '--out-dir',
        help='Path to the directory where to install the SDK',
        required=True)
    parser.add_argument(
        '--stamp-file', help='Path to the victory file', required=True)
    parser.add_argument(
        '--manifest', help='Path to the SDK\'s manifest file', required=True)
    args = parser.parse_args()

    # Remove any existing output.
    shutil.rmtree(args.out_dir, True)

    with open(args.manifest, 'r') as manifest_file:
        mappings = map(
            lambda l: l.strip().split('=', 1), manifest_file.readlines())

    for dest, source in mappings:
        destination = os.path.join(args.out_dir, dest)
        make_dir(destination)
        shutil.copy2(source, destination)

    with open(args.stamp_file, 'w') as stamp_file:
        stamp_file.write('Now go use it\n')


if __name__ == '__main__':
    sys.exit(main())
