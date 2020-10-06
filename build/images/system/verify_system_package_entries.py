#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--list', help='Path to the file listing entries', required=True)
    parser.add_argument('--stamp', help='Path to the stamp file', required=True)
    args = parser.parse_args()

    with open(args.list, 'r') as list_file:
        lines = list_file.readlines()

    if not lines:
        with open(args.stamp, 'w') as stamp_file:
            stamp_file.write('Comparison successful \o/')
        return 0

    def format_line(line):
        # Format: @obj/foo/bar/target_name.system.rsp
        line = '/' + line[4:].strip()
        line = line[:-11]
        last_slash = line.rfind('/')
        line = line[:last_slash] + ':' + line[last_slash + 1:]
        return line

    lines = sorted(map(format_line, lines))

    print(
        '----------------------------------------------------------------------'
        '\n'
        'The following packages contribute to the system package but are not'
        ' included in the build properly:'
        '\n')
    for line in lines:
        print(line)
    print(
        '\n'
        'Packages introduced in board files should be added to the'
        ' "board_system_image_deps" variable, not "board_package_labels".'
        '\n'
        'Packages introduced in product files should be added to the'
        ' "product_system_image_deps" variable, not any of the'
        ' "foo_package_labels" variants.')


if __name__ == '__main__':
    sys.exit(main())
