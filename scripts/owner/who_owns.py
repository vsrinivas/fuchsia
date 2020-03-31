#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(  # scripts
        SCRIPT_DIR))  # owner

owner_exp = re.compile('^\s*([a-zA-Z0-9_.+-]+@[a-zA-Z0-9-]+\.[a-zA-Z0-9-.]+)')
perfile_exp = re.compile(
    '^\s*per-file ([^\s=]*)\s*=\s*([a-zA-Z0-9_.+-]+@[a-zA-Z0-9-]+\.'
    '[a-zA-Z0-9-.]+)')


# $ ./who_owns.py file1
# owner1@example.com,owner2@example.com
#
# $ ./who_owns.py file2
# owner3@example.com
#
# $ ./who_owns.py file1 file2
# owner1@example.com,owner2@example.com,owner3@example.com
def main():
    parser = argparse.ArgumentParser(
        description='Finds all OWNERS of `paths`')
    parser.add_argument('paths', nargs='+')
    args = parser.parse_args()
    abspaths = [os.path.abspath(path) for path in args.paths]

    # Find all OWNERS files
    owners_paths = set()
    for path in abspaths:
        dir = path if os.path.isdir(path) else os.path.dirname(path)
        dir = os.path.abspath(dir)
        while (os.path.exists(dir) and
               os.path.commonprefix([dir, FUCHSIA_ROOT]) == FUCHSIA_ROOT):
            owners_path = os.path.join(dir, 'OWNERS')
            if os.path.exists(owners_path):
                owners_paths.add(owners_path)
                break
            dir = os.path.dirname(dir)

    # Parse all OWNERS files
    owners = set()
    for path in owners_paths:
        with open(path) as f:
            for line in f.readlines():
                match = owner_exp.match(line)
                if match:
                    owners.add(match.group(1))
                    continue
                match = perfile_exp.match(line)
                if match:
                    filename = os.path.abspath(
                        os.path.join(os.path.dirname(path), match.group(1)))
                    if filename in abspaths:
                        owners.add(match.group(2))

    print ','.join(sorted(owners))

    return 0


if __name__ == '__main__':
    sys.exit(main())
