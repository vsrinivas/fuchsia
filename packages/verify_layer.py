#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
from common import FUCHSIA_ROOT, get_package_imports
import json
import os
import sys


def check_json(packages):
    '''Verifies that all files in the list are JSON files.'''
    all_json = True
    for package in packages:
        with open(package, 'r') as file:
            try:
                json.load(file)
            except ValueError:
                all_json = False
                print('Non-JSON file: %s.' % package)
    return all_json


def check_deps_exist(dep_map):
    '''Verifies that all dependencies exist.'''
    all_exist = True
    for (package, deps) in dep_map.iteritems():
        for dep in deps:
            if not os.path.isfile(dep):
                all_exist = False
                print('Dependency of %s does not exist: %s.' % (package, dep))
    return all_exist


def check_all(directory, dep_map):
    '''Verifies that directories contain an "all" package and that this packages
       lists all the files in the directory.
       '''
    is_clean = True
    for dirpath, dirnames, filenames in os.walk(directory):
        # The 'config' directory does not need any aggregation.
        dirnames = [d for d in dirnames if d != 'config']
        for dir in dirnames:
            is_clean = is_clean and check_all(os.path.join(dirpath, dir),
                                              dep_map)
        if not is_clean:
            return False
        all_package = os.path.join(dirpath, 'all')
        if not os.path.isfile(all_package):
            print('Directory does not contain an "all" package: %s.' % dirpath)
            return False
        known_deps = dep_map[all_package]
        has_all_files = True
        def verify(package):
            if package not in known_deps:
                print('Missing dependency in %s: %s.' % (all_package, package))
                return False
            return True
        for file in filenames:
            package = os.path.join(dirpath, file)
            if package == all_package:
                continue
            if not verify(package):
                has_all_files = False
        for dir in dirnames:
            package = os.path.join(dirpath, dir, 'all')
            if not verify(package):
                has_all_files = False
        return has_all_files


def main():
    parser = argparse.ArgumentParser(
            description=('Checks that packages in a given layer are properly '
                         'formatted and organized'))
    parser.add_argument('--layer',
                        help='Name of the layer to analyze',
                        choices=['garnet', 'peridot', 'topaz'],
                        required=True)
    args = parser.parse_args()

    layer = args.layer
    os.chdir(FUCHSIA_ROOT)
    base = os.path.join(layer, 'packages')

    # List all packages files.
    packages = []
    for dirpath, dirnames, filenames in os.walk(base):
        packages.extend([os.path.join(dirpath, f) for f in filenames])

    if not check_json(packages):
        return False

    deps = dict([(p, get_package_imports(p)) for p in packages])

    if not check_deps_exist(deps):
        return False

    if not check_all(base, deps):
        return False

    return True


if __name__ == '__main__':
    return_code = 0
    if not main():
        print('Errors!')
        return_code = 1
    sys.exit(return_code)
