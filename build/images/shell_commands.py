#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# Lint as: python3

import argparse
import json
import os
import sys


# Generates shebang files for binaries so they can be invoked from the shell.
# Generated files look like this:
# #!resolve fuchsia-pkg://fuchsia.com/<package-name>#bin/<bin-name>
def main():
    parser = argparse.ArgumentParser(
        'Generate shebang files for shell binaries')
    parser.add_argument('--package-name', required=True)
    parser.add_argument(
        '--input-distribution-manifest',
        required=True,
        type=argparse.FileType('r'),
        help='Path to a distribution manifest with all shell binaries')
    parser.add_argument(
        '--output-directory',
        required=True,
        help='Path to a directory to store generated shebang files')
    parser.add_argument(
        '--output-distribution-manifest',
        required=True,
        type=argparse.FileType('w'),
        help='Path to write output distribution manifest')
    parser.add_argument(
        '--depfile',
        required=True,
        type=argparse.FileType('w'),
        help='Depfile for listing generated shebang files as additional inputs')
    args = parser.parse_args()

    input_dist = json.load(args.input_distribution_manifest)
    output_dist = []

    for dist in input_dist:
        dest = dist["destination"]
        if not dest.startswith("bin/"):
            continue
        shebang_file = os.path.join(args.output_directory, dest)
        os.makedirs(os.path.dirname(shebang_file), exist_ok=True)
        with open(shebang_file, 'w') as f:
            f.write(
                f'#!resolve fuchsia-pkg://fuchsia.com/{args.package_name}#{dest}\n'
            )
        output_dist.append(dict(dist, source=shebang_file))

    json.dump(
        output_dist,
        args.output_distribution_manifest,
        indent=2,
        sort_keys=True,
        separators=(',', ': '))

    args.depfile.write(
        args.output_distribution_manifest.name + ": " +
        ' '.join(dist['source'] for dist in output_dist) + '\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
