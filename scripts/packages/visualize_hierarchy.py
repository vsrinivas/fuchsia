#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
from common import FUCHSIA_ROOT, get_package_imports
import json
import os
import sys


def get_package_id(package):
    return package.replace('/', '_').replace('-', '_')


def get_package_nick(package):
    parts = package.split('/')
    base = parts.index('packages')
    return '/'.join(parts[base+1:])


def main():
    parser = argparse.ArgumentParser(
            description=('Creates a graph of a build package hierarchy'))
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--product',
                        help='Path to the build product file to analyze')
    group.add_argument('--package',
                        help='Path to the build package file to analyze')
    parser.add_argument('--output',
                        help='Path to the generated .dot file',
                        required=True)
    args = parser.parse_args()

    packages = []
    if args.product:
        with open(args.product) as product_file:
            data = json.load(product_file)
            packages.extend(data['monolith'] if 'monolith' in data else [])
            packages.extend(data['preinstall'] if 'preinstall' in data else [])
            packages.extend(data['available'] if 'available' in data else [])
    else:
        packages = [args.package]

    # Build the dependency tree of packages.
    deps = {}
    while packages:
        current = packages.pop(0)
        imports = get_package_imports(current)
        deps[current] = imports
        new_packages = [p for p in imports if p not in deps]
        packages.extend(new_packages)

    layers = {}
    for package in deps:
        parts = package.split('/')
        try:
            package_base = parts.index('packages')
        except:
            raise Exception('Unexpected directory structure for %s' % package)
        layer = '/'.join(parts[0:package_base])
        layers.setdefault(layer, []).append(package)

    with open(args.output, 'w') as out:
        out.write('digraph fuchsia {\n')
        for index, pair in enumerate(layers.iteritems()):
            layer, packages = pair
            out.write('subgraph cluster_%s {\n' % index)
            out.write('label="%s";\n' % layer)
            for package in packages:
                out.write('%s [label="%s"];\n' % (get_package_id(package),
                                                  get_package_nick(package)))
            for package in packages:
                dep_ids = [get_package_id(d) for d in deps[package]]
                out.write('%s -> { %s }\n' % (get_package_id(package),
                                              ' '.join(dep_ids)))
            out.write('}\n')
        out.write('}\n')


if __name__ == '__main__':
    sys.exit(main())
