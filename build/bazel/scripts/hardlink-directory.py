#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Copy the content of an existing source directory, relative to the Fuchsis source tree
to a Bazel repository, using hard-links if possible. This is to be invoked by a Bazel
repository rule.
"""

import argparse
import os
import shutil
import sys

_SCRIPT_DIR = os.path.dirname(__file__)
_FUCHSIA_DIR = os.path.abspath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_SCRIPT_NAME = os.path.relpath(os.path.abspath(__file__), _FUCHSIA_DIR)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--fuchsia-dir', help='Specify alternate Fuchsia root path.')
    parser.add_argument(
        'source_dir', help='Source directory, relative to Fuchsia root path.')
    parser.add_argument('destination_dir', help='Destination directory.')
    args = parser.parse_args()

    if args.fuchsia_dir:
        fuchsia_dir = os.path.abspath(fuchsia_dir)
    else:
        fuchsia_dir = _FUCHSIA_DIR

    src_dir = os.path.join(fuchsia_dir, args.source_dir)
    dst_dir = os.path.abspath(args.destination_dir)

    if not os.path.exists(src_dir):
        return parser.error('Source directory does not exist: ' + src_dir)

    # Using hard-links could fail due copying to a different mount point,
    # so first try with hard links, then do a normal copy otherwise.
    try:
        shutil.copytree(
            src_dir, dst_dir, copy_function=os.link, dirs_exist_ok=True)
    except OSError as error:
        shutil.copytree(src_dir, dst_dir, dirs_exist_ok=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
