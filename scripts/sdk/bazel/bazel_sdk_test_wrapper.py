#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(
        description='Wrapper to run the Bazel SDK tests.')
    parser.add_argument(
        '--test_file',
        required=True,
        type=Path,
        help='Path to the Bazel test file.')
    parser.add_argument(
        '--bazel', required=True, type=Path, help='Path to the Bazel tool.')
    args = parser.parse_args()

    # Check output will throw an exception if the subprocess command fails.
    output = subprocess.check_output([args.test_file, '--bazel', args.bazel])
    return 0


if __name__ == '__main__':
    sys.exit(main())
