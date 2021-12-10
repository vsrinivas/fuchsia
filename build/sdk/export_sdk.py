#!/usr/bin/env python3.8
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
    parser.add_argument('--depfile', help='Path to depfile', required=True)
    args = parser.parse_args()

    # Remove any existing outputs. Manually removing all subdirectories and
    # files instead of using shutil.rmtree, to avoid registering spurious reads
    # on stale subdirectories.
    if os.path.exists(args.out_dir):
        for root, dirs, files in os.walk(args.out_dir, topdown=False):
            for file in files:
                os.unlink(os.path.join(root, file))
            for dir in dirs:
                os.rmdir(os.path.join(root, dir))

    with open(args.manifest, 'r') as manifest_file:
        mappings = [l.strip().split('=', 1) for l in manifest_file.readlines()]

    sources, destinations = [], []
    for dest, source in mappings:
        destination = os.path.join(args.out_dir, dest)
        make_dir(destination)
        # Absolute path needed to symlink correctly.
        os.symlink(os.path.abspath(source), destination)
        # Relative path required in depfile.
        sources.append(os.path.relpath(source))
        destinations.append(destination)

    with open(args.stamp_file, 'w') as stamp_file:
        stamp_file.write('Now go use it\n')

    with open(args.depfile, 'w') as depfile:
        depfile.write(
            '{} {}: {}\n'.format(
                args.stamp_file, ' '.join(destinations), ' '.join(sources)))


if __name__ == '__main__':
    sys.exit(main())
