#!/usr/bin/env python
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
    parser.add_argument("--print-sdk-path",
        action="store_true", dest="print_sdk_path", default=False,
        help="Print the path to the SDK")
    parser.add_argument("--developer-dir", help='Path to Xcode')
    parser.add_argument("min_sdk_version", help="Minimum SDK version")
    args = parser.parse_args()

    if args.developer_dir:
      os.environ['DEVELOPER_DIR'] = args.developer_dir

    job = subprocess.Popen(['xcode-select', '-print-path'],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT)
    out, err = job.communicate()
    if job.returncode != 0:
        print >> sys.stderr, out
        print >> sys.stderr, err
        raise Exception('Error %d running xcode-select' % job.returncode)

    sdk_dir = os.path.join(
        out.rstrip(), 'Platforms/MacOSX.platform/Developer/SDKs')
    try:
        sdks = [re.findall('^MacOSX(10\.\d+)\.sdk$', s) for s in os.listdir(sdk_dir)]
    except OSError:
        raise Exception('Mac OSX SDK does not seem to be selected. ' +
            'Run "sudo xcode-select -r" and try again.')
    sdks = [s[0] for s in sdks if s]
    sdks = [s for s in sdks
        if parse_version(s) >= parse_version(args.min_sdk_version)]
    if not sdks:
        raise Exception('No %s+ SDK found' % args.min_sdk_version)
    sdk = sorted(sdks, key=parse_version)[0]

    if args.print_sdk_path:
        print subprocess.check_output(
            ['xcrun', '-sdk', 'macosx' + sdk, '--show-sdk-path']).strip()

    print sdk

    return 0


if __name__ == '__main__':
    if sys.platform != 'darwin':
        raise Exception("This script only runs on Mac")
    sys.exit(main())
