#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
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
    parser.add_argument('--sources', help='List of library sources', nargs='*')
    parser.add_argument('--headers', help='List of public headers', nargs='*')
    parser.add_argument(
        '--include-dir', help='Path to the include directory', required=True)
    args = parser.parse_args()

    metadata = {
        'deps': args.deps,
        'headers': args.headers,
        'include_dir': args.include_dir,
        'name': args.name,
        'root': args.root,
        'sources': args.sources,
        'type': 'cc_source_library',
    }

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
