#!/usr/bin/env python2.7
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Prints the lowest locally available SDK version greater than or equal to a
given minimum sdk version to standard output. If --developer-dir is passed, then
the script will use the Xcode toolchain located at DEVELOPER_DIR.

Usage:
  python find_sdk.py [--developer-dir DEVELOPER_DIR] 10.12
"""

import argparse
import os
import re
import subprocess
import sys


def parse_version(version_str):
    """'10.6' => [10, 6]"""
    return map(int, re.findall(r'(\d+)', version_str))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--print-sdk-path",
        action="store_true",
        dest="print_sdk_path",
        default=False,
        help="Print the path to the SDK")
    parser.add_argument("--developer-dir", help='Path to Xcode')
    parser.add_argument("min_sdk_version", help="Minimum SDK version")
    args = parser.parse_args()

    if args.developer_dir:
        os.environ['DEVELOPER_DIR'] = args.developer_dir

    # 'xcrun' always returns the latest available SDK
    version = subprocess.check_output(
        ['xcrun', '--sdk', 'macosx', '--show-sdk-version']).strip()
    if parse_version(version) < parse_version(args.min_sdk_version):
        raise Exception(
            'SDK version %s is before minimum version %s' %
            (version, args.min_sdk_version))
    if args.print_sdk_path:
        print subprocess.check_output(
            ['xcrun', '--sdk', 'macosx', '--show-sdk-path']).strip()
    print version

    return 0


if __name__ == '__main__':
    if sys.platform != 'darwin':
        raise Exception("This script only runs on Mac")
    sys.exit(main())
