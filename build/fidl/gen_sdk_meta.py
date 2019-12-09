#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser('Builds a metadata file')
    parser.add_argument('--out', help='Path to the output file', required=True)
    parser.add_argument('--name', help='Name of the library', required=True)
    parser.add_argument(
        '--root', help='Root of the library in the SDK', required=True)
    parser.add_argument(
        '--specs', help='Path to spec files of dependencies', nargs='*')
    parser.add_argument('--sources', help='List of library sources', nargs='+')
    args = parser.parse_args()

    metadata = {
        'type': 'fidl_library',
        'name': args.name,
        'root': args.root,
        'sources': args.sources,
    }

    deps = []
    for spec in args.specs:
        with open(spec, 'r') as spec_file:
            data = json.load(spec_file)
        type = data['type']
        name = data['name']
        if type == 'fidl_library':
            deps.append(name)
        else:
            raise Exception('Unsupported dependency type: %s' % type)
    metadata['deps'] = deps

    with open(args.out, 'w') as out_file:
        json.dump(
            metadata,
            out_file,
            indent=2,
            sort_keys=True,
            separators=(',', ': '))

    return 0


if __name__ == '__main__':
    sys.exit(main())
