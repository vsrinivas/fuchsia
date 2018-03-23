#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


class Library(object):

    def __init__(self, name, path, file):
        self.name = name
        self.path = path
        self.file = file

    def __str__(self):
        return '%s[%s]' % (self.name, self.path)

    def __hash__(self):
        return hash((self.name, self.path))

    def __eq__(self, other):
        return self.name == other.name and self.path == other.path


def get_libraries(dep_files, extra_library=None):
    # Aggregate library data from dependencies.
    libraries = set()
    if extra_library:
        libraries.add(extra_library)
    for dep in dep_files:
        with open(dep, 'r') as dep_file:
            for name, path in json.load(dep_file).iteritems():
                libraries.add(Library(name, path, dep))

    # Verify duplicates.
    libs_by_name = {}
    for lib in libraries:
        libs_by_name.setdefault(lib.name, []).append(lib)
    for name, libs in libs_by_name.iteritems():
        if len(libs) <= 1:
            continue
        print('Error: library "%s" has multiple paths.' % name)
        for lib in libs:
            print(' - %s (%s)' % (lib.path, lib.file))
        raise Exception('Could not aggregate libraries')

    return dict([(l.name, l.path) for l in libraries])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name',
                        help='Name of the current library',
                        required=True)
    parser.add_argument('--source-dir',
                        help='Path to the library\'s source directory',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the file to generate',
                        required=True)
    parser.add_argument('--deps',
                        help='Dependencies of the current library',
                        nargs='*')
    args = parser.parse_args()

    current_library = Library(args.name, args.source_dir, args.output)
    result = get_libraries(args.deps, extra_library=current_library)
    with open(args.output, 'w') as output_file:
        json.dump(result, output_file, indent=2, sort_keys=True)


if __name__ == '__main__':
    sys.exit(main())
