#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys

sys.path.append(os.path.join(
    os.path.dirname(__file__),
    os.pardir,
    "cpp",
))
import binaries


def main():
    parser = argparse.ArgumentParser('Builds a metadata file')
    parser.add_argument('--base',
                        help='Path to the base metadata file',
                        required=True)
    parser.add_argument('--out',
                        help='Path to the output file',
                        required=True)
    parser.add_argument('--lib-debug-file',
                        help='Path to the source debug version of the library',
                        action='append')
    parser.add_argument('--debug-mapping',
                        help='Path to the file where to write the file mapping for the debug library',
                        required=True)
    args = parser.parse_args()

    debug_files = []

    with open(args.debug_mapping, 'w') as mappings_file:
        for debug_file in args.lib_debug_file:
            debug_path = binaries.get_sdk_debug_path(debug_file)
            mappings_file.write(debug_path + '=' + debug_file + '\n')
            debug_files.append(debug_path)

    with open(args.base, 'r') as base_file:
        metadata = json.load(base_file)

    metadata['versions'].values()[0]['debug_libs'] = debug_files

    with open(args.out, 'w') as out_file:
        json.dump(metadata, out_file, indent=2, sort_keys=True)

    return 0


if __name__ == '__main__':
    sys.exit(main())
