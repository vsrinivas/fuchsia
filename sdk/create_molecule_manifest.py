#!/usr/bin/env python
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
    parser.add_argument('--out',
                        help='Path to the output file',
                        required=True)
    parser.add_argument('--deps',
                        help='List of manifest paths for the included elements',
                        nargs='*')
    parser.add_argument('--is-group',
                        help='True if the molecule is a grouping of its deps',
                        action='store_true')
    parser.add_argument('--metadata',
                        help='Metadata to attach to the manifest',
                        action='append',
                        default=[])
    parser.add_argument('--category',
                        help='Minimum publication level',
                        required=False)
    args = parser.parse_args()

    (direct_deps, atoms) = gather_dependencies(args.deps)
    if detect_collisions(atoms):
        print('Name collisions detected!')
        return 1
    if args.category:
        if detect_category_violations(args.category, atoms):
            print('Publication level violations detected!')
            return 1
    ids = []
    if args.is_group:
        ids = map(lambda i: i.json, sorted(list(direct_deps)))
    manifest = {
        'ids': ids,
        'atoms': map(lambda a: a.json, sorted(list(atoms))),
    }
    if args.metadata:
        manifest['meta'] = dict(map(lambda m: m.split('=', 1), args.metadata))
    with open(os.path.abspath(args.out), 'w') as out:
        json.dump(manifest, out, indent=2, sort_keys=True)


if __name__ == '__main__':
    sys.exit(main())
