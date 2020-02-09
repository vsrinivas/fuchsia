#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import fileinput
import os
import re
import sys


def main():
    parser = argparse.ArgumentParser(
            description='Insert blank lines between target deps in build files')
    parser.add_argument('--file',
                        help='path to the BUILD.gn file',
                        required=True)
    parser.add_argument('--target',
                        help='name of the target to process',
                        required=True)
    args = parser.parse_args()

    if os.path.basename(args.file) != 'BUILD.gn':
        print('Err, %s is not a build file!' % args.file)
        return 1

    in_target = True
    bracket_count = 0
    for line in fileinput.FileInput(args.file, inplace=True):
        match = re.match('^\s*([a-z][a-z_]*[a-z])\("([^"]+)"\)\s?{$', line)
        if match:
            if match.group(2) == args.target:
                in_target = True
                bracket_count = 1
            sys.stdout.write(line)
            continue
        if not in_target:
            sys.stdout.write(line)
            continue
        bracket_count += line.count('{')
        bracket_count -= line.count('}')
        if bracket_count <= 0:
            in_target = False
            bracket_count = 0
        if in_target:
            match = re.match('^\s*"[^"]+",\s*$', line)
            if match:
                sys.stdout.write('#------------------\n')
                sys.stdout.write(line)
                sys.stdout.write('#------------------\n')
                continue
        sys.stdout.write(line)

    return 0


if __name__ == '__main__':
    sys.exit(main())
