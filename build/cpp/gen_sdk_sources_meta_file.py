#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys

from collections import defaultdict


def resolve_plasa_files(args):
    """Resolves the plasa fragment file names from a list of files with reference
       content.

    Args:
        plasa: list(string): list of file names containing pointers to generated
        plasa fragment files.

    Returns:
        A sorted set of resolved plasa fragment files.
    """
    plasa = []
    if not args:
        return plasa
    for arg in args:
        with open(arg, 'r') as plasa_file:
            data = json.load(plasa_file)
            for d in data:
                plasa += [d['path']]
    return sorted(set(plasa))


def main():
    parser = argparse.ArgumentParser('Builds a metadata file')
    parser.add_argument('--out', help='Path to the output file', required=True)
    parser.add_argument('--name', help='Name of the library', required=True)
    parser.add_argument(
        '--root', help='Root of the library in the SDK', required=True)
    parser.add_argument(
        '--deps', help='Path to metadata files of dependencies', nargs='*')
    parser.add_argument(
        '--dep_names', help='List of dependency names', nargs='*')
    parser.add_argument('--sources', help='List of library sources', nargs='*')
    # Allowed to have no headers, since SDK libraries included in other SDK
    # libraries could have no headers.
    parser.add_argument('--headers', help='List of public headers', nargs='*')
    parser.add_argument(
        '--include-dir', help='Path to the include directory', required=True)
    parser.add_argument(
        '--plasa', help='Path to the plasa fragments list', nargs='*')
    args = parser.parse_args()

    if len(args.deps) != len(args.dep_names):
        raise Exception(
            'Length of deps %s != length of dep_names %s' %
            (len(args.deps), len(args.dep_names)))

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
    banjo_deps = []
    fidl_deps = []
    banjo_deps = []
    fidl_layers = defaultdict(list)
    for idx, spec in enumerate(args.deps):
        with open(spec, 'r') as spec_file:
            data = json.load(spec_file)
        if not data:
            continue
        type = data['type']
        name = data['name']
        if type == 'cc_source_library' or type == 'cc_prebuilt_library':
            deps.append(name)
        elif type == 'fidl_library':
            dep_name = args.dep_names[idx]
            if dep_name.endswith('banjo_cpp'):
                banjo_deps.append(name)
            else:
                fidl_deps.append(name)
                # Layer here is defined to be "cpp" plus everything after
                # "_cpp" in the dependency name
                if "_cpp" in dep_name:
                    layer = "cpp" + dep_name.split("_cpp", maxsplit=1)[1]
                    fidl_layers[layer].append(name)
                else:
                    fidl_layers["hlcpp"].append(name)
        else:
            raise Exception('Unsupported dependency type: %s' % type)

    metadata['deps'] = sorted(set(deps))
    metadata['banjo_deps'] = sorted(set(banjo_deps))
    metadata['fidl_deps'] = sorted(set(fidl_deps))
    metadata['plasa'] = resolve_plasa_files(args.plasa)
    metadata['fidl_binding_deps'] = [
        {
            "binding_type": layer,
            "deps": sorted(set(dep))
        } for layer, dep in fidl_layers.items()
    ]

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
