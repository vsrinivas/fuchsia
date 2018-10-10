#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import sys

# NOTE: deprecated, use `generate.py --tests` instead.

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

from create_test_workspace import create_test_workspace


def main():
    parser = argparse.ArgumentParser(
            description=('Generates some tests for the Bazel SDK'))
    parser.add_argument('--sdk',
                        help='Path to the SDK to test',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the directory where to install the tests',
                        required=True)
    args = parser.parse_args()

    if not create_test_workspace(args.sdk, args.output):
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
