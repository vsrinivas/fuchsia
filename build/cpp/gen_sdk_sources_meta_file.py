#!/usr/bin/env python3.8
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
        '--deps', help='Path to metadata files of dependencies', nargs='*')
    parser.add_argument('--sources', help='List of library sources', nargs='+')
    parser.add_argument('--headers', help='List of public headers', nargs='*')
    parser.add_argument(
        '--include-dir', help='Path to the include directory', required=True)
    args = parser.parse_args()

    metadata = {
        'type': 'cc_source_library',
        'name': args.name,
        'root': args.root,
        'sources': args.sources,
        'headers': args.headers,
        'include_dir': args.include_dir,
        'banjo_deps': [],
    }

    deps = []
    fidl_deps = []
    for spec in args.deps:
        with open(spec, 'r') as spec_file:
            data = json.load(spec_file)
        if not data:
            continue
        type = data['type']
        name = data['name']
        if type == 'cc_source_library' or type == 'cc_prebuilt_library':
            deps.append(name)
        elif type == 'fidl_library':
            fidl_deps.append(name)
        else:
            raise Exception('Unsupported dependency type: %s' % type)
    metadata['deps'] = sorted(set(deps))
    metadata['fidl_deps'] = sorted(set(fidl_deps))

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
