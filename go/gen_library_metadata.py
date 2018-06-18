#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


class Source(object):

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


def get_sources(dep_files, extra_sources=None):
    # Aggregate source data from dependencies.
    sources = set()
    if extra_sources:
        sources.update(extra_sources)
    for dep in dep_files:
        with open(dep, 'r') as dep_file:
            for name, path in json.load(dep_file).iteritems():
                sources.add(Source(name, path, dep))

    # Verify duplicates.
    sources_by_name = {}
    for src in sources:
        sources_by_name.setdefault(src.name, []).append(src)
    for name, srcs in sources_by_name.iteritems():
        if len(srcs) <= 1:
            continue
        print('Error: source "%s" has multiple paths.' % name)
        for src in srcs:
            print(' - %s (%s)' % (src.path, src.file))
        raise Exception('Could not aggregate sources')

    return dict([(s.name, s.path) for s in sources])


def main():
    parser = argparse.ArgumentParser()
    name_group = parser.add_mutually_exclusive_group(required=True)
    name_group.add_argument('--name',
                            help='Name of the current library')
    name_group.add_argument('--name-file',
                            help='Path to a file containing the name of the current library')
    parser.add_argument('--source-dir',
                        help='Path to the library\'s source directory',
                        required=True)
    parser.add_argument('--sources',
                        help='List of source files',
                        nargs='*')
    parser.add_argument('--output',
                        help='Path to the file to generate',
                        required=True)
    parser.add_argument('--deps',
                        help='Dependencies of the current library',
                        nargs='*')
    args = parser.parse_args()
    if args.name:
        name = args.name
    elif args.name_file:
        with open(args.name_file, 'r') as name_file:
            name = name_file.read()

    current_sources = []
    if args.sources:
        # TODO(BLD-62): verify that the sources are in a single folder.
        for source in args.sources:
            current_sources.append(Source(os.path.join(name, source),
                                          os.path.join(args.source_dir, source),
                                          args.output))
    else:
        current_sources.append(Source(name, args.source_dir, args.output))
    result = get_sources(args.deps, extra_sources=current_sources)
    with open(args.output, 'w') as output_file:
        json.dump(result, output_file, indent=2, sort_keys=True)


if __name__ == '__main__':
    sys.exit(main())
