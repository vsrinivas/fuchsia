#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

import argparse
import os.path
import sys

# Exempt targets with these prefixes.
EXEMPTION_PREFIXES = [
    # TODO(fxbug.dev/56885): cargo-gnaw should generate sources files for third party crates.
    '//third_party/rust_crates:',
]


def parse_depfile(depfile_path):
    with open(depfile_path) as f:
        # Only the first line contains important information.
        line = f.readline().strip()

    # The depfile format looks like `target: dep1 dep2 dep3...`
    # We assume the target is the one we care about and that dep1, dep2, dep3, etc. are
    # source files.
    #
    # We use `os.path.relpath` to convert paths like '../../out/default/foo/bar' to the
    # canonical 'foo/bar', which is how the expected inputs are expressed.
    return set(
        os.path.relpath(os.path.normpath(source))
        for source in line[line.find(':') + 1:].split(' ')
        if source.strip())


def build_file_source_path(target, source_path):
    '''Returns a source path suitable for listing in a BUILD.gn file.

    The returned path is relative to the `target` GN label, or source absolute if the
    `source_path` is not a descendent of the `target`.

    Eg. for `target` of '//src/sys/component_manager:bin':
      when source_path='../../src/sys/component_manager/src/main.rs'
        return 'src/main.rs'
      when source_path='../../prebuilts/assets/font.ttf'
        return '//prebuilts/assets/font.ttf'
    '''
    while source_path.startswith('../'):
        source_path = source_path[3:]
    target_dir = target[2:].split(':')[0]
    if source_path.startswith(target_dir):
        return os.path.relpath(source_path, start=target_dir)
    return '//{}'.format(source_path)


def print_suggested_sources(varname, sources):
    '''Prints a GN list variable assignment with the variable name `varname`.

    Eg.

      sources = [
        "src/main.rs",
        "src/foo.rs",
      ]
    '''
    print('  {} = ['.format(varname), file=sys.stderr)
    for source in sources:
        print('    "{}",'.format(source), file=sys.stderr)
    print('  ]', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description=
        'Verifies that the compiler-emitted depfile strictly contains the expected source files'
    )
    parser.add_argument(
        '-t',
        '--target_label',
        required=True,
        help='GN target label being checked')
    parser.add_argument(
        '-d',
        '--depfile',
        required=True,
        help='path to compiler emitted depfile')
    parser.add_argument(
        'expected_sources',
        nargs='*',
        help='path to the expected list of source files')
    args = parser.parse_args()

    # Check for opt-out.
    if args.expected_sources and args.expected_sources[0].endswith(
            '/build/rust/__SKIP_ENFORCEMENT__.rs'):
        return 0

    # Ignore specific target exemptions.
    for prefix in EXEMPTION_PREFIXES:
        if args.target_label.startswith(prefix):
            return 0

    expected_sources = set(args.expected_sources)
    actual_sources = parse_depfile(args.depfile)

    unlisted_sources = actual_sources.difference(expected_sources)
    if unlisted_sources:
        # There is a mismatch in expected sources and actual sources used by the compiler.
        # We don't treat overly-specified sources as an error. Ninja will still complain
        # if those source files don't exist.
        for source in unlisted_sources:
            print(
                'error: source file `{}` was used during compilation but not listed in BUILD.gn'
                .format(source),
                file=sys.stderr)

        print(
            'note: the BUILD.gn file for {} should have the following:\n'.
            format(args.target_label),
            file=sys.stderr)

        rust_sources = [
            build_file_source_path(args.target_label, source)
            for source in actual_sources
            if source.endswith('.rs')
        ]
        if rust_sources:
            print_suggested_sources('sources', rust_sources)

        non_rust_sources = [
            build_file_source_path(args.target_label, source)
            for source in actual_sources
            if not source.endswith('.rs')
        ]
        if non_rust_sources:
            print_suggested_sources('inputs', non_rust_sources)

        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
