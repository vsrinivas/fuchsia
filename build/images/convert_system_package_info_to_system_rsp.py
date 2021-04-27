#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Converts a .json file listing fini manifests into a .system.rsp file.'''

import argparse
import collections
import json
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--input', help='Path to input .json file', required=True)
    parser.add_argument(
        '--output', help='Path to the output formatted manifest', required=True)
    parser.add_argument('--depfile', help='Path to the output depfile')
    parser.add_argument(
        '--filter-packages',
        choices=('none', 'system_only', 'extras'),
        default='none',
        help='How to validate packages that appear in the input file.')
    args = parser.parse_args()

    # Load a JSON file which is a list of dictionaries with the following
    # schema:
    #
    #   label: fuchsia_system_package() full GN label, without toolchain
    #   fini_manifest: path to corresponding .fini manifest describing the
    #     package's content.
    #   system_image_package_allowed_in_extra_deps: optional. Only defined for
    #     fuchsia_system_package() instance. The value the determines whether
    #     this is allowed in extra dependency trees.
    #
    # Packages without a system_package_class are ignored, unless
    # --filter-packages=system_only is used.
    #
    with open(args.input, 'r') as input_file:
        system_package_infos = json.load(input_file)

    system_packages = []
    extra_system_packages = []
    regular_packages = []
    for info in system_package_infos:
        allowed = info.get('system_image_package_allowed_in_extra_deps', None)
        if allowed:
            extra_system_packages.append(info)
        elif allowed is not None:
            system_packages.append(info)
        else:  # allowed is None
            regular_packages.append(info)

    # Filter the packages list if needed, and print a human-friendly error
    # message that explains how to fix the issues, if some exist.
    if args.filter_packages == 'system_only' and regular_packages:
        packages = sorted(p['label'] for p in regular_packages)
        print(
            'ERROR: The following fuchsia_package() targets should not be part of the //build/input:system_image\n'
            +
            'dependency tree. Either remove them from it, or convert them to a fuchsia_system_package() instance:\n  '
            + '\n  '.join(packages) + '\n',
            file=sys.stderr)
        return 1

    if args.filter_packages == 'extras' and system_packages:
        packages = sorted(p['label'] for p in system_packages)
        print(
            'ERROR: The following fuchsia_system_package() are list from either //base/images:base_packages\n'
            +
            'or //base/images:meta_packages. This is only allowed if their definition includes an\n'
            +
            '\'allowed_in_extra_deps = true\' argument. Either fix it, or remove the package from the\n'
            + 'dependency tree:\n  ' + '\n  '.join(packages) + '\n',
            file=sys.stderr)
        return 1

    # All clear, generate the final output now.
    output = ''
    depfile = args.output + ':'
    for info in system_packages + extra_system_packages:
        output += "--entry-manifest=%s\n" % info['label']
        fini_manifest = info['fini_manifest']
        depfile += ' %s' % fini_manifest
        with open(fini_manifest) as f:
            for line in f.readlines():
                if line.startswith('meta/'):
                    continue
                output += '--entry=' + line

    with open(args.output, 'w') as f:
        f.write(output)

    if args.depfile:
        with open(args.depfile, 'w') as f:
            f.write(depfile)


if __name__ == '__main__':
    sys.exit(main())
