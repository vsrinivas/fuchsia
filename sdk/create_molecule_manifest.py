#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out',
                        help='Path to the output file',
                        required=True)
    parser.add_argument('--deps',
                        help='List of manifest paths for the included elements',
                        nargs='*')
    parser.add_argument('--tags',
                        help='List of tags for the included elements',
                        nargs='*')
    args = parser.parse_args()

    def add_tags(atom):
        existing_tags = set(atom['tags'])
        atom['tags'] = list(existing_tags.union(args.tags))
        return atom

    atoms = []
    for dep in args.deps:
        with open(dep, 'r') as dep_file:
            manifest = json.load(dep_file)
            if manifest['type'] == 'atom':
                atoms.append(add_tags(manifest))
            else:
                for atom in manifest['atoms']:
                    atoms.append(add_tags(atom))

    manifest = {
        'type': 'molecule',
        'atoms': atoms,
    }
    with open(os.path.abspath(args.out), 'w') as out:
        json.dump(manifest, out, indent=2, sort_keys=True)


if __name__ == '__main__':
    sys.exit(main())
