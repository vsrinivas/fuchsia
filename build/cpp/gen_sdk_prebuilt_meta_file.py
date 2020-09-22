#!/usr/bin/env python3.8
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
    parser.add_argument('--out', help='Path to the output file', required=True)
    parser.add_argument('--name', help='Name of the library', required=True)
    parser.add_argument(
        '--format',
        help='Format of the library',
        choices=['shared', 'static'],
        required=True)
    parser.add_argument(
        '--root', help='Root of the library in the SDK', required=True)
    parser.add_argument(
        '--deps', help='Path to metadata files of dependencies', nargs='*')
    parser.add_argument('--headers', help='List of public headers', nargs='*')
    parser.add_argument(
        '--include-dir', help='Path to the include directory', required=True)
    parser.add_argument(
        '--arch', help='Name of the target architecture', required=True)
    parser.add_argument(
        '--lib-link',
        help='Path to the link-time library in the SDK',
        required=True)
    parser.add_argument(
        '--lib-dist',
        help='Path to the library to add to Fuchsia packages in the SDK',
        required=False)
    parser.add_argument(
        '--dist-path',
        help='Path to the library in Fuchsia packages',
        required=False)
    parser.add_argument(
        '--lib-debug-file',
        help='Path to the source debug version of the library',
        required=False)
    parser.add_argument(
        '--debug-mapping',
        help=
        'Path to the file where to write the file mapping for the debug library',
        required=False)
    args = parser.parse_args()

    metadata = {
        'type': 'cc_prebuilt_library',
        'name': args.name,
        'root': args.root,
        'format': args.format,
        'headers': args.headers,
        'include_dir': args.include_dir,
    }
    metadata['binaries'] = {
        args.arch: {
            'link': args.lib_link,
        },
    }

    if args.lib_debug_file:
        # The path of the debug file in the SDK depends on its build id.
        debug_path = binaries.get_sdk_debug_path(args.lib_debug_file)
        with open(args.debug_mapping, 'w') as mappings_file:
            mappings_file.write(debug_path + '=' + args.lib_debug_file + '\n')
        metadata['binaries'][args.arch]['debug'] = debug_path

    if args.lib_dist:
        metadata['binaries'][args.arch]['dist'] = args.lib_dist
        metadata['binaries'][args.arch]['dist_path'] = args.dist_path

    deps = []
    for spec in args.deps:
        with open(spec, 'r') as spec_file:
            data = json.load(spec_file)
        type = data['type']
        name = data['name']
        # TODO(fxbug.dev/5365): verify that source libraries are header-only.
        if type == 'cc_source_library' or type == 'cc_prebuilt_library':
            deps.append(name)
        else:
            raise Exception('Unsupported dependency type: %s' % type)
    metadata['deps'] = sorted(set(deps))

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
