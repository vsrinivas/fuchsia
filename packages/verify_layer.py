#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
from common import FUCHSIA_ROOT, get_package_imports
import json
import os
import subprocess
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# Standard names for root packages in a layer.
ROOT_CANONICAL_PACKAGES = [
    'buildbot',
    'default',
    'kitchen_sink',
]

# Standard names for packages in a layer.
CANONICAL_PACKAGES = [
    'all',
]

# Directories which do not require aggregation.
NO_AGGREGATION_DIRECTORIES = [
    'config',
    'disabled',
    # TODO: force experimental content to be built for each layer.
    'experimental',
    'products',
]

# Non-package files allowed in package directories.
NON_PACKAGE_FILES = [
    'README.md',
]


def check_json(packages):
    '''Verifies that all files in the list are JSON files.'''
    all_json = True
    for package in packages:
        with open(package, 'r') as file:
            try:
                json.load(file)
            except ValueError:
                all_json = False
                print('Non-JSON file: %s' % package)
    return all_json


def check_schema(packages, validator):
    '''Verifies that all files adhere to the schema.'''
    all_valid = True
    schema = os.path.join(SCRIPT_DIR, 'package_schema.json')
    for package in packages:
        if subprocess.call([validator, schema, package]) != 0:
            all_valid = False
    return all_valid


def check_deps_exist(dep_map):
    '''Verifies that all dependencies exist.'''
    all_exist = True
    for (package, deps) in dep_map.iteritems():
        for dep in deps:
            if not os.path.isfile(dep):
                all_exist = False
                print('Dependency of %s does not exist: %s' % (package, dep))
    return all_exist


def check_all(directory, dep_map, layer, is_root=True):
    '''Verifies that directories contain an "all" package and that this packages
       lists all the files in the directory.
       '''
    for dirpath, dirnames, filenames in os.walk(directory):
        dirnames = [d for d in dirnames if d not in NO_AGGREGATION_DIRECTORIES]
        is_clean = True
        for dir in dirnames:
            subdir = os.path.join(dirpath, dir)
            if not check_all(subdir, dep_map, layer, is_root=False):
                is_clean = False
        if not is_clean:
            return False
        all_package = os.path.join(dirpath, 'all')
        if not os.path.isfile(all_package):
            print('Directory does not contain an "all" package: %s' % dirpath)
            return False
        known_deps = dep_map[all_package]
        has_all_files = True
        def verify(package):
            if package not in known_deps:
                print('Missing dependency in %s: %s' % (all_package, package))
                return False
            return True
        for file in filenames:
            if is_root and (file in ROOT_CANONICAL_PACKAGES or file == layer):
                continue
            if file in CANONICAL_PACKAGES or file in NON_PACKAGE_FILES:
                continue
            package = os.path.join(dirpath, file)
            if not verify(package):
                has_all_files = False
        for dir in dirnames:
            package = os.path.join(dirpath, dir, 'all')
            if not verify(package):
                has_all_files = False
        return has_all_files


def check_no_fuchsia_packages_in_all(packages):
    allowed_keys = {'imports'}
    all_clear = True
    for package in [p for p in packages if os.path.basename(p) == 'all']:
        with open(package, 'r') as file:
            data = json.load(file)
            keys = set(data.keys())
            if not keys.issubset(allowed_keys):
                all_clear = False
                print('"all" should only contain imports: %s' % package)
    return all_clear


def check_root(base, layer):
    '''Verifies that all canonical packages are present at the root.'''
    all_there = True
    for file in ROOT_CANONICAL_PACKAGES + [layer]:
        if not os.path.isfile(os.path.join(base, file)):
            all_there = False
            print('Missing root package: %s' % file)
    return all_there


def main():
    parser = argparse.ArgumentParser(
            description=('Checks that packages in a given layer are properly '
                         'formatted and organized'))
    layer_group = parser.add_mutually_exclusive_group(required=True)
    layer_group.add_argument('--layer',
                             help='Name of the layer to analyze',
                             choices=['garnet', 'peridot', 'topaz'])
    layer_group.add_argument('--vendor-layer',
                             help='Name of the vendor layer to analyze')
    parser.add_argument('--json-validator',
                        help='Path to the JSON validation tool',
                        required=True)
    args = parser.parse_args()

    os.chdir(FUCHSIA_ROOT)
    if args.layer:
        layer = args.layer
        base = os.path.join(layer, 'packages')
    else:
        layer = args.vendor_layer
        base = os.path.join('vendor', layer, 'packages')

    # List all packages files.
    packages = []
    for dirpath, dirnames, filenames in os.walk(base):
        packages.extend([os.path.join(dirpath, f) for f in filenames
                         if f not in NON_PACKAGE_FILES])

    if not check_json(packages):
        return False

    if not check_schema(packages, args.json_validator):
        return False

    deps = dict([(p, get_package_imports(p)) for p in packages])

    if not check_deps_exist(deps):
        return False

    if not check_all(base, deps, layer):
        return False

    if not check_no_fuchsia_packages_in_all(packages):
        return False

    if not check_root(base, layer):
        return False

    return True


if __name__ == '__main__':
    return_code = 0
    if not main():
        print('Errors!')
        return_code = 1
    sys.exit(return_code)
