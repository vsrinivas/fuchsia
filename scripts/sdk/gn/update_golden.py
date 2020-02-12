#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Update golden files for GN SDK for use in tests.

Executes generate.py against the testdata/ directory and copies the output to
the golden directory. Run this after making changes to generator code and
include the updated files in your commit.
"""

import generate
import os
import shutil
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GOLDEN_DIR = os.path.join(SCRIPT_DIR, 'golden')
TMP_DIR_NAME = tempfile.mkdtemp(prefix='tmp_gn_sdk_golden')


def main():
    testdata = os.path.join(SCRIPT_DIR, 'testdata')
    # Generator follows bash convention and returns a non-zero value on error.
    if generate.run_generator(output=TMP_DIR_NAME,
                              archive='',
                              directory=testdata):
        print('generate.py returned an error')
        return 1

    # Remove the existing files.
    if os.path.exists(GOLDEN_DIR):
        shutil.rmtree(GOLDEN_DIR)

    # Ignore bin and build, which are copied from base.
    shutil.copytree(src=TMP_DIR_NAME, dst=GOLDEN_DIR,
        ignore=shutil.ignore_patterns('bin', 'build'))

    # Special case: copy build/test_targets.gni from outdir
    golden_build = os.path.join(GOLDEN_DIR, 'build')
    os.makedirs(golden_build)
    shutil.copy2(src=os.path.join(TMP_DIR_NAME, 'build', 'test_targets.gni'),
        dst=golden_build)

    # Cleanup.
    if os.path.exists(TMP_DIR_NAME):
        shutil.rmtree(TMP_DIR_NAME)

    return 0


if __name__ == '__main__':
    sys.exit(main())
