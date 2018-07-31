#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys

# Uses the Dart snapshotter to identify the source files of a library based on
# the list of its public files.

def get_dependencies(args):
    result = []
    return result


def main():
    parser = argparse.ArgumentParser(
            description='Identifies the sources of a Dart package')
    parser.add_argument('--gen-snapshot',
                        help='Path to the Dart snapshotter',
                        required=True)
    parser.add_argument('--sources',
                        help='List of public sources for the library',
                        nargs="*")
    parser.add_argument('--packages',
                        help='Path to the .packages file',
                        required=True)
    parser.add_argument('--source-dir',
                        help='Path to the library\'s source directory',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the output file listing the sources',
                        required=True)
    parser.add_argument('--depfile',
                        help='Path to the depfile to generate',
                        required=True)
    parser.add_argument('--depname',
                        help='Name of the target in the depfile',
                        required=True)
    parser.add_argument('--url-mapping',
                        help='Additional mappings for built-in libraries',
                        nargs='*')
    args = parser.parse_args()

    all_deps = get_dependencies(args)
    def is_within_package(dep):
        return os.path.commonprefix([dep, args.source_dir]) == args.source_dir
    local_deps = filter(is_within_package, all_deps)
    # Add the original sources in case they were not under the source directory.
    local_deps = list(set(local_deps) | set(args.sources))
    local_deps.sort()

    with open(args.output, 'w') as output_file:
        for dep in local_deps:
            output_file.write('%s\n' % dep)

    with open(args.depfile, 'w') as dep_file:
        dep_file.write('%s: %s' % (args.depname, ' '.join(local_deps)))


if __name__ == '__main__':
    sys.exit(main())
