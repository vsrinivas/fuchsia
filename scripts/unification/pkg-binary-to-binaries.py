#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Converts instances of //build/package.gni to use the `binaries` parameter
   instead of the deprecated `binary`.
   '''

import argparse
import fileinput
import os
import re
import sys

from common import fx_format


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--base',
        help='Path to the directory in which files will be fixed',
        required=True)
    args = parser.parse_args()

    # Fix manifest files.
    manifest_paths = []
    for root, _, files in os.walk(args.base):
        for file in files:
            name, extension = os.path.splitext(file)
            if extension != '.cmx':
                continue
            file_path = os.path.join(root, file)
            with open(file_path, 'r') as manifest_file:
                contents = manifest_file.read()
            if '"bin/app"' not in contents:
                continue
            contents = contents.replace('"bin/app"', '"bin/' + name + '"')
            with open(file_path, 'w') as manifest_file:
                manifest_file.write(contents)
            manifest_paths.append(file_path)

    # Fix build files.
    build_changes = {}  # file path --> list of affected binaries
    for root, _, files in os.walk(args.base):
        for file in files:
            if file != 'BUILD.gn':
                continue
            file_path = os.path.join(root, file)
            found_reference = False
            for line in fileinput.FileInput(file_path, inplace=True):
                match = re.match('^(\s*)binary = "([^"]+)"\s*$', line)
                if match:
                    found_reference = True
                    line = match.group(
                        1) + 'binaries = [{ name = "' + match.group(2) + '" }]'
                    build_changes.setdefault(file_path,
                                             []).append(match.group(2))
                sys.stdout.write(line)
            if found_reference:
                fx_format(file_path)

    # Cross-reference the two lists of modified files and attempt to identify
    # issues.
    print('-----------------------------')
    print('Updated %d component manifest files' % len(manifest_paths))
    print(
        'Updated %d references in %d build files' % (
            sum([len(v) for v in build_changes.values()],
                0), len(build_changes.keys())))
    print('')

    # First off, build a list of manifest files we expect to see given the
    # changes made to build files.
    expected_manifest_paths = []
    matched_manifest_paths = []
    for build_file, binaries in sorted(build_changes.iteritems()):
        base_dir = os.path.dirname(build_file)
        for binary in binaries:
            manifest = os.path.join(base_dir, 'meta', binary + '.cmx')
            if manifest in manifest_paths:
                # We found a manifest exactly where we expected it: great!
                manifest_paths.remove(manifest)
                matched_manifest_paths.append(manifest)
                continue
            if '_' in binary:
                # Since '_' is technically not allowed in package URIs, some
                # manifest files are renamed by turning '_' into '-'.
                alternate_manifest = os.path.join(
                    base_dir, 'meta',
                    binary.replace('_', '-') + '.cmx')
                if alternate_manifest in manifest_paths:
                    manifest_paths.remove(alternate_manifest)
                    matched_manifest_paths.append(alternate_manifest)
                    continue
            expected_manifest_paths.append(manifest)
    print('-----------------------------')
    print(
        'After exact matches: %d references, %d manifest files unmatched' %
        (len(expected_manifest_paths), len(manifest_paths)))
    print('Matches:')
    for path in matched_manifest_paths:
        print(path)
    print('')

    # Second step is to look at manifests with the same file name as what we
    # would expect AND under the same directory.
    print('-----------------------------')
    print('Likely matches')
    for potential_path in list(expected_manifest_paths):
        base_dir = os.path.dirname(os.path.dirname(potential_path))
        name = os.path.basename(potential_path)
        for path in list(manifest_paths):
            if os.path.commonprefix([
                    base_dir, path
            ]) == base_dir and os.path.basename(path) == name:
                expected_manifest_paths.remove(potential_path)
                manifest_paths.remove(path)
                print('[---] ' + potential_path)
                print('[+++] ' + path)
                break
    print('')

    print('-----------------------------')
    print(
        'Leftovers: %d references, %d manifest files' %
        (len(expected_manifest_paths), len(manifest_paths)))
    combined = [(p, '---') for p in expected_manifest_paths] + [
        (p, '+++') for p in manifest_paths
    ]
    for path, type in sorted(combined):
        print('[%s] %s' % (type, path))

    return 0


if __name__ == '__main__':
    sys.exit(main())
