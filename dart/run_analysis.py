#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser('Runs analysis on a given package')
    parser.add_argument('--source-dir', help='Path to package source',
                        required=True)
    parser.add_argument('--dot-packages', help='Path to the .packages file',
                        required=True)
    parser.add_argument('--dartanalyzer',
                        help='Path to the Dart analyzer executable',
                        required=True)
    parser.add_argument('--dart-sdk', help='Path to the Dart SDK',
                        required=True)
    parser.add_argument('--options', help='Path to analysis options',
                        required=True)
    parser.add_argument('--stamp', help='File to touch when analysis succeeds',
                        required=True)
    parser.add_argument('--depname', help='Name of the depfile target',
                        required=True)
    parser.add_argument('--depfile', help='Path to the depfile to generate',
                        required=True)
    args = parser.parse_args()

    with open(args.depfile, 'w') as depfile:
        depfile.write('%s: ' % args.depname)
        for dirpath, dirnames, filenames in os.walk(args.source_dir):
            for filename in filenames:
                _, extension = os.path.splitext(filename)
                if extension == '.dart':
                    depfile.write('%s ' % (os.path.join(dirpath, filename)))

    call_args = [
        args.dartanalyzer,
        '--packages=%s' % args.dot_packages,
        '--dart-sdk=%s' % args.dart_sdk,
        '--options=%s' % args.options,
        args.source_dir,
        '--fatal-warnings',
        '--fatal-hints',
        '--fatal-lints',
    ]

    call = subprocess.Popen(call_args, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    stdout, stderr = call.communicate()
    if call.returncode:
        print(stdout + stderr)
        return 1

    with open(args.stamp, 'w') as stamp:
        stamp.write('Success!')


if __name__ == '__main__':
    sys.exit(main())
