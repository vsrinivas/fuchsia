#!/usr/bin/env python3.8
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys
import json
import yaml


def main():
    parser = argparse.ArgumentParser('Runs analysis on a given package')
    parser.add_argument(
        '--source-file', help='Path to the list of sources', required=True)
    parser.add_argument(
        '--dot-packages', help='Path to the .packages file', required=True)
    parser.add_argument(
        '--dart', help='Path to the Dart executable', required=True)
    parser.add_argument(
        '--stamp',
        type=argparse.FileType('w'),
        help='File to touch when analysis succeeds',
        required=True)
    parser.add_argument(
        '--depname', help='Name of the depfile target', required=True)
    parser.add_argument(
        '--depfile',
        type=argparse.FileType('w'),
        help='Path to the depfile to generate',
        required=True)
    parser.add_argument(
        '--all-deps-sources-file',
        type=argparse.FileType('r'),
        help=
        'Path to a file containing sources this target and all of its direct and transitive dependencies',
        required=True)
    args = parser.parse_args()

    with open(args.source_file, 'r') as source_file:
        # Normalize path so paths like `foo/bar/../main.dart` would work even if
        # `foo/bar` doesn't exist.
        sources = [
            os.path.normpath(p) for p in source_file.read().strip().split('\n')
        ]

    args.depfile.write(
        '{}: {}'.format(
            args.depname,
            ' '.join(json.load(args.all_deps_sources_file)),
        ),
    )

    call_args = [
        args.dart,
        'analyze',
        '--packages={}'.format(args.dot_packages),
        '--fatal-warnings',
        '--fatal-infos',
    ] + sources

    # Call Dart anaylzer.
    call = subprocess.Popen(
        call_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = call.communicate()

    # Convert output to strings, assuming UTF-8 output from dart.
    stdout = stdout.decode('utf-8')
    stderr = stderr.decode('utf-8')

    if call.returncode:
        print(stdout + stderr)
        return 1

    args.stamp.write('Success!')
    return 0


if __name__ == '__main__':
    sys.exit(main())
