#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # sdk
    SCRIPT_DIR)))                # bazel


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

    # Remove any existing output.
    shutil.rmtree(args.output, True)

    shutil.copytree(os.path.join(SCRIPT_DIR, 'tests'), args.output)

    with open(os.path.join(args.output, 'WORKSPACE'), 'w') as workspace_file:
        workspace_file.write('''# This is a generated file.

local_repository(
    name = "fuchsia_sdk",
    path = "%s",
)

load("@fuchsia_sdk//build_defs:crosstool.bzl", "install_fuchsia_crosstool")
install_fuchsia_crosstool(
    name = "fuchsia_crosstool",
)

load("@fuchsia_sdk//build_defs:setup_dart.bzl", "setup_dart")
setup_dart()
''' % os.path.relpath(args.sdk, args.output))


if __name__ == '__main__':
    sys.exit(main())
