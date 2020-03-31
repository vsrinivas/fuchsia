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


# $ what_do_i_own.py me@mydomain file_i_own file_i_dont
# file_i_own
def main():
    parser = argparse.ArgumentParser(
        description='Filters `paths` for those owned by `owner`')
    parser.add_argument('owner')
    parser.add_argument('paths', nargs='+')
    args = parser.parse_args()
    owner = args.owner
    abspaths = [os.path.abspath(path) for path in args.paths]

    perfile_exp = re.compile('^\s*per-file ([^\s=]*)\s*=\s*' + owner)

    # Find all OWNERS files
    path_to_owners = {}
    for path in abspaths:
        dir = path if os.path.isdir(path) else os.path.dirname(path)
        dir = os.path.abspath(dir)
        while (os.path.exists(dir) and
               os.path.commonprefix([dir, FUCHSIA_ROOT]) == FUCHSIA_ROOT):
            owners_path = os.path.join(dir, 'OWNERS')
            if os.path.exists(owners_path):
                path_to_owners[path] = owners_path
                break
            dir = os.path.dirname(dir)

    # Parse all OWNERS files
    owned = set()
    for path, owners in path_to_owners.iteritems():
        with open(owners) as f:
            for line in f.readlines():
                if line.strip().startswith(owner):
                    owned.add(path)
                    continue
                match = perfile_exp.match(line)
                if match:
                    filename = os.path.abspath(
                        os.path.join(os.path.dirname(owners), match.group(1)))
                    if filename in abspaths:
                        owned.add(path)

    # Print owned files
    for owned_path in sorted(owned):
        print os.path.relpath(owned_path)

    return 0


if __name__ == '__main__':
    sys.exit(main())
