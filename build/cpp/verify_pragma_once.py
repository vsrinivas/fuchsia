#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--headers',
        help='The list of header files to check',
        default=[],
        nargs='*')
    parser.add_argument(
        '--stamp',
        help='The path to the stamp file in case of success',
        required=True)
    args = parser.parse_args()

    has_errors = False

    matcher = re.compile(r'^#pragma once')
    for header in args.headers:
        with open(header, 'r') as header_file:
            for line in header_file.readlines():
                if matcher.match(line):
                    print('Error: pragma disallowed in SDK: %s' % header)
                    has_errors = True
    if has_errors:
        return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Success!')
    return 0


if __name__ == '__main__':
    sys.exit(main())
