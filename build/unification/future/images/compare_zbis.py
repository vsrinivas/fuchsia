#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Compares the contents of two zbi files, erroring if they differ.'''

import argparse
import collections
import filecmp
import re
import subprocess
import sys

BootfsEntry = collections.namedtuple('BootfsEntry', ['path', 'size', 'offset'])
Zbi = collections.namedtuple('Zbi', ['bootfs', 'cmdline'])

header_matcher = re.compile('^[0-9a-f]{8}: [0-9a-f]{8} (\w+).*$')


def parse_zbi(data):
    bootfs = []
    cmdline = []
    current_header = None
    for line in data.splitlines():
        header_match = header_matcher.match(line)
        if header_match:
            current_header = header_match.group(1)
        elif current_header == 'BOOTFS':
            bootfs_match = re.match(
                '^\s{8}: ([0-9a-f]{8}) ([0-9a-f]{8}) (.+)$', line)
            if bootfs_match:
                entry = BootfsEntry(
                    path=bootfs_match.group(3),
                    size=int(bootfs_match.group(2), 16),
                    offset=int(bootfs_match.group(1), 16))
                bootfs.append(entry)
        elif current_header == 'CMDLINE':
            cmd_match = re.match('^\s{8}: ([^\s]+)$', line)
            if cmd_match:
                cmdline.append(cmd_match.group(1))
    return Zbi(bootfs=set(bootfs), cmdline=set(cmdline))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tool', help='Path to the zbi tool', required=True)
    parser.add_argument(
        '--original-zbi', help='Path to the original zbi', required=True)
    parser.add_argument(
        '--updated-zbi', help='Path to the updated zbi', required=True)
    parser.add_argument(
        '--stamp',
        help='Path to the stamp file signalling success',
        required=True)
    args = parser.parse_args()

    def get_contents(path):
        command = [args.tool, '-tv', path]
        return parse_zbi(subprocess.check_output(command))

    original = get_contents(args.original_zbi)
    updated = get_contents(args.updated_zbi)
    original_by_path = dict((e.path, e) for e in original.bootfs)
    updated_by_path = dict((e.path, e) for e in updated.bootfs)
    original_paths = set(original_by_path.keys())
    updated_paths = set(updated_by_path.keys())

    has_errors = False

    if original_paths != updated_paths:
        original_only = original_paths - updated_paths
        if original_only:
            has_errors = True
            print('Paths only in the original zbi:')
            for path in sorted(original_only):
                print(' - ' + path)
        updated_only = updated_paths - original_paths
        if updated_only:
            has_errors = True
            print('Paths only in the updated zbi:')
            for path in sorted(updated_only):
                print(' - ' + path)

    size_mismatches = [
        (p, original_by_path[p].size, updated_by_path[p].size)
        for p in original_paths & updated_paths
    ]
    size_mismatches = [(p, r, g) for p, r, g in size_mismatches if r != g]
    if size_mismatches:
        has_errors = True
        print('Size mismatches:')
        for path, original_size, updated_size in sorted(size_mismatches):
            print(
                ' - ' + path + ' (' + str(original_size) + ' vs. ' +
                str(updated_size) + ')')

    if original.cmdline != updated.cmdline:
        has_errors = True
        original_only = original.cmdline - updated.cmdline
        if original_only:
            print('Command line arguments only in the original zbi:')
            for cmd in sorted(original_only):
                print(' - ' + cmd)
        updated_only = updated.cmdline - original.cmdline
        if updated_only:
            print('Command line arguments only in the updated zbi:')
            for cmd in sorted(updated_only):
                print(' - ' + cmd)

    if not has_errors:
        # Just in case, check that the files are absolutely identical.
        if not filecmp.cmp(args.original_zbi, args.updated_zbi):
            has_errors = True

    if has_errors:
        print('The ZBI files did not match!')
        return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Success!')
    return 0


if __name__ == '__main__':
    sys.exit(main())
