#!/usr/bin/env python3.8
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
            for name, path in json.load(dep_file).items():
                sources.add(Source(name, path, dep))

    # Verify duplicates.
    sources_by_name = {}
    for src in sources:
        sources_by_name.setdefault(src.name, []).append(src)
    for name, srcs in sources_by_name.items():
        if len(srcs) <= 1:
            continue
        print('Error: source "%s" has multiple paths.' % name)
        for src in srcs:
            print(' - %s (%s)' % (src.path, src.file))
        raise Exception('Could not aggregate sources')

    return {s.name: s.path for s in sources}


def main():
    parser = argparse.ArgumentParser()
    name_group = parser.add_mutually_exclusive_group(required=True)
    name_group.add_argument('--name', help='Name of the current library')
    name_group.add_argument(
        '--name-file',
        help='Path to a file containing the name of the current library')
    parser.add_argument(
        '--source-dir',
        help='Path to the library\'s source directory',
        required=True)
    sources_group = parser.add_mutually_exclusive_group(required=True)
    sources_group.add_argument(
        '--sources', help='List of source files', nargs='*')
    sources_group.add_argument(
        '--allow-globbing',
        action='store_true',
        help='Allow globbing the entire source directory')
    parser.add_argument(
        '--output', help='Path to the file to generate', required=True)
    parser.add_argument(
        '--deps', help='Dependencies of the current library', nargs='*')
    args = parser.parse_args()
    if args.name:
        name = args.name
    elif args.name_file:
        with open(args.name_file, 'r') as name_file:
            name = name_file.read()

    current_sources = []
    if args.sources:
        for source in args.sources:
            p = os.path.join(args.source_dir, source)
            # Explicit sources must be files.
            if not os.path.isfile(p):
                raise ValueError(f'Source {p} is not a file')
            current_sources.append(
                Source(os.path.join(name, source), p, args.output))
        if not name.endswith('/...'):
            go_sources = {f for f in args.sources if f.endswith('.go')}

            # Go sources are constrained to live top-level under `source_dir`;
            # others (e.g., template files) are free to live further down.
            for s in go_sources:
                if os.path.dirname(s):
                    raise ValueError(
                        f'Source "{s}" for "{name}" comes from a subdirectory.'
                        f' Specify source_dir instead.')

            # TODO: Use `glob.glob("*.go", root_dir=args.source_dir)` instead of
            # os.listdir after upgrading to Python 3.10.
            go_files = {
                f for f in os.listdir(args.source_dir) if f.endswith('.go')
            }
            missing = go_files - go_sources
            if missing:
                raise ValueError(
                    f'go_library requires that all Go files in source_dir be listed'
                    f' as sources, but the following files are missing from sources'
                    f' for target {name}: {", ".join(sorted(missing))}')
    elif args.allow_globbing:
        current_sources.append(Source(name, args.source_dir, args.output))
    result = get_sources(args.deps, extra_sources=current_sources)
    with open(args.output, 'w') as output_file:
        json.dump(result, output_file, indent=2, sort_keys=True)


if __name__ == '__main__':
    sys.exit(main())
