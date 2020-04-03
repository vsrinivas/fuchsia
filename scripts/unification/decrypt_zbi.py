#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import os
import re
import subprocess
import sys


_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    _SCRIPT_DIR))                # unification


BootfsEntry = collections.namedtuple('BootfsEntry', ['name', 'size'])


def parse_zbi(data):
    bootfs = []
    current_header = None
    for line in data.splitlines():
        header_match = re.match('^[0-9a-f]{8}: [0-9a-f]{8} (\w+).*$', line)
        if header_match:
            current_header = header_match.group(1)
            continue
        if current_header == 'BOOTFS':
            bootfs_match = re.match('^\s{8}: [0-9a-f]{8} ([0-9a-f]{8}) (.+)$',
                                    line)
            if bootfs_match:
                bootfs.append(BootfsEntry(name=bootfs_match.group(2),
                                          size=int(bootfs_match.group(1), 16)))
    return bootfs


def main():
    parser = argparse.ArgumentParser(description='Displays ZBI contents')
    parser.add_argument('zbi',
                        help='Path to the zbi file to inspect')
    parser.add_argument('--build-dir',
                        help='Path to the build directory',
                        default=os.path.join(FUCHSIA_ROOT, 'out', 'default'))
    parser.add_argument('--mode',
                        help='Sorting dimension',
                        choices=['size', 'name'],
                        default='name')
    args = parser.parse_args()

    zbi_tool = os.path.join(args.build_dir, 'host_x64', 'zbi')
    contents = subprocess.check_output([zbi_tool, '-tv', args.zbi])
    bootfs = parse_zbi(contents)

    if args.mode == 'size':
        for b in sorted(bootfs, key=lambda b: b.size, reverse=True):
            size = '{:,}'.format(b.size)
            print(size.rjust(11) + ' ' + b.name)
    elif args.mode == 'name':
        for b in sorted(bootfs):
            print(b.name)

    return 0


if __name__ == '__main__':
    sys.exit(main())
