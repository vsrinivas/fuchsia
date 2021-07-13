#/usr/bin/env python3.8

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A script for updating BUILD.gn file for third-party golibs.

This script traverses source directories for all targets and prints updated
BUILD.gn to stdout.

TODO(https://github.com/golang/go/issues/42504): Switch to use `go list` when it
supports listing without build constraints.
"""

import argparse
import glob
import os
import sys


def is_source(f):
    """Returns true iff the input file name is considered a source file."""
    return not f.endswith('_test.go') and (
        f.endswith('.go') or f.endswith('.s'))


def all_sources(golibs_dir, lib_name):
    """Returns all source paths to all source files of a library.

    Paths are relative to library root: vendor/{lib_name}
    """
    lib_root = os.path.join(golibs_dir, 'vendor', lib_name)
    return (
        os.path.relpath(f, lib_root)
        for f in glob.glob(os.path.join(lib_root, '**', '*'), recursive=True)
        if is_source(f)
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--build-file',
        help='The BUILD.gn file to update',
        type=argparse.FileType('r'),
        required=True,
    )
    parser.add_argument(
        '--golibs-dir',
        help='Root directory for third-party golibs',
        required=True,
    )
    args = parser.parse_args()

    lib_name = ''
    skip = False
    for line in args.build_file:
        stripped_line = line.strip()

        if stripped_line.startswith('go_library("'):
            lib_name = line.split('"')[1]
            print(line.rstrip())
            continue

        if skip:
            if stripped_line.endswith(']'):
                skip = False
            continue

        # Skip old sources because we'll rewrite the list with updated ones.
        if stripped_line.startswith('sources = ['):
            # There's only one source.
            if stripped_line.endswith(']'):
                continue
            skip = True
            continue

        # Add sources right before the target ends.
        if stripped_line == '}':
            sources = all_sources(args.golibs_dir, lib_name)
            print('  sources = [')
            print('\n'.join(f'    "{src}",' for src in sorted(sources)))
            print('  ]')

        # Leave all other lines untouched.
        print(line.rstrip())


if __name__ == '__main__':
    sys.exit(main())
