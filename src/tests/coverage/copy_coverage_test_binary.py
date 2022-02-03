#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
A helper script for copying instrumented coverage test binary to the given output path.
"""
import argparse
import shutil
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--input',
        help='File that contains filepath to instrumented coverage test binary',
        required=True)
    parser.add_argument(
        '--output',
        help='Path to copy the instrumented coverage test binary',
        required=True)
    parser.add_argument(
        '--depfile',
        help='Path to write a depfile, see depfile from GN',
        required=True)
    args = parser.parse_args()

    # Read the path to the instrumented coverage test binary.
    with open(args.input, 'r') as f:
        coverage_test_binary_path = f.read().strip()

    # Copy instrumented coverage test binary to the given output path.
    shutil.copy(coverage_test_binary_path, args.output)

    # Write depfile.
    with open(args.depfile, 'w') as f:
        f.write(f'{args.output}: {coverage_test_binary_path}\n')


if __name__ == '__main__':
    sys.exit(main())
