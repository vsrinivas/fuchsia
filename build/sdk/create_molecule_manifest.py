#!/usr/bin/env python2.7
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys

from sdk_common import detect_category_violations, detect_collisions, gather_dependencies


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', help='Path to the output file', required=True)
    parser.add_argument(
        '--deps',
        help='List of manifest paths for the included elements',
        nargs='*')
    parser.add_argument(
        '--category', help='Minimum publication level', required=False)
    args = parser.parse_args()

    (direct_deps, atoms) = gather_dependencies(args.deps)
    if detect_collisions(atoms):
        print('Name collisions detected!')
        return 1
    if args.category:
        if detect_category_violations(args.category, atoms):
            print('Publication level violations detected!')
            return 1
    manifest = {
        'ids': [],
        'atoms': map(lambda a: a.json, sorted(list(atoms))),
    }
    with open(os.path.abspath(args.out), 'w') as out:
        json.dump(
            manifest, out, indent=2, sort_keys=True, separators=(',', ': '))


if __name__ == '__main__':
    sys.exit(main())
