#!/usr/bin/env python3.8
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys

FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # build
    os.path.dirname(             # dart
    os.path.abspath(__file__))))

if sys.version_info[0] >= 3:
  sys.path += [os.path.join(FUCHSIA_ROOT, 'third_party', 'pyyaml', 'lib3')]
else:
  sys.path += [os.path.join(FUCHSIA_ROOT, 'third_party', 'pyyaml', 'lib')]

import yaml


def main():
    parser = argparse.ArgumentParser('Runs analysis on a given package')
    parser.add_argument(
        '--source-file', help='Path to the list of sources', required=True)
    parser.add_argument(
        '--dot-packages', help='Path to the .packages file', required=True)
    parser.add_argument(
        '--dartanalyzer',
        help='Path to the Dart analyzer executable',
        required=True)
    parser.add_argument(
        '--dart-sdk', help='Path to the Dart SDK', required=True)
    parser.add_argument(
        '--options', help='Path to analysis options', required=True)
    parser.add_argument(
        '--stamp', help='File to touch when analysis succeeds', required=True)
    parser.add_argument(
        '--depname', help='Name of the depfile target', required=True)
    parser.add_argument(
        '--depfile', help='Path to the depfile to generate', required=True)
    args = parser.parse_args()

    with open(args.source_file, 'r') as source_file:
        sources = source_file.read().strip().split('\n')

    with open(args.depfile, 'w') as depfile:
        depfile.write('%s: ' % args.depname)

        def add_dep(path):
            depfile.write('%s ' % path)

        options = args.options
        while True:
            if not os.path.isabs(options):
                print('Expected absolute path, got %s' % options)
                return 1
            if not os.path.exists(options):
                print('Could not find options file: %s' % options)
                return 1
            add_dep(options)
            with open(options, 'r') as options_file:
                content = yaml.safe_load(options_file)
                if not 'include' in content:
                    break
                included = content['include']
                if not os.path.isabs(included):
                    included = os.path.join(os.path.dirname(options), included)
                options = included

    call_args = [
        args.dartanalyzer,
        '--packages=%s' % args.dot_packages,
        '--dart-sdk=%s' % args.dart_sdk,
        '--options=%s' % args.options,
        '--fatal-warnings',
        '--fatal-hints',
        '--fatal-lints',
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

    with open(args.stamp, 'w') as stamp:
        stamp.write('Success!')


if __name__ == '__main__':
    sys.exit(main())
