#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Symlink the content of an existing source directory, relative to the Fuchsis
source tree to a Bazel repository. This is to be invoked by a Bazel repository
rules and must not have any external dependencies.
"""

import argparse
import os
import shutil
import sys

_SCRIPT_DIR = os.path.dirname(__file__)
_FUCHSIA_DIR = os.path.abspath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_SCRIPT_NAME = os.path.relpath(os.path.abspath(__file__), _FUCHSIA_DIR)


def force_symlink(target_path, link_path):
    target_path = os.path.relpath(target_path, os.path.dirname(link_path))
    try:
        os.symlink(target_path, link_path)
    except OSError as e:
        if e.errno == errno.EEXIST:
            os.remove(link_path)
            os.symlink(ltarget_path, link_path)
        else:
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--fuchsia-dir', help='Specify alternate Fuchsia root path.')
    parser.add_argument(
        'source_dir', help='Source directory, relative to Fuchsia root path.')
    parser.add_argument('destination_dir', help='Destination directory.')
    parser.add_argument(
        '--recursive', action='store_true', help='Perform recursive walk')
    args = parser.parse_args()

    if args.fuchsia_dir:
        fuchsia_dir = os.path.abspath(fuchsia_dir)
    else:
        fuchsia_dir = _FUCHSIA_DIR

    src_dir = os.path.join(fuchsia_dir, args.source_dir)
    dst_dir = os.path.abspath(args.destination_dir)

    if not os.path.exists(src_dir):
        return parser.error('Source directory does not exist: ' + src_dir)

    if args.recursive:
        shutil.copytree(
            src_dir, dst_dir, copy_function=force_symlink, dirs_exist_ok=True)
    else:
        for entry in os.listdir(src_dir):
            src_path = os.path.join(src_dir, entry)
            dst_path = os.path.join(dst_dir, entry)
            force_symlink(src_path, dst_path)

    return 0


if __name__ == "__main__":
    sys.exit(main())
