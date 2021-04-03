#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
A wrapper script for setting up the environment for
verify_zbi_kernel_cmdline_test.

Usage:
test_wrapper.py python_exe_path test_script scrutiny_path zbi_path
"""

import os
import subprocess
import sys


def main(args):
    python_path = os.path.abspath(args[0])
    test_script = os.path.abspath(args[1])
    os.environ['SCRUTINY'] = os.path.abspath(args[2])
    os.environ['ZBI'] = os.path.abspath(args[3])
    dir_path = os.path.dirname(os.path.realpath(__file__))
    if not os.path.exists(os.environ['SCRUTINY']):
        print('scrutiny is not found at ' + os.environ['SCRUTINY'])
    if not os.path.exists(os.environ['ZBI']):
        print('zbi is not found at ' + os.environ['ZBI'])

    subprocess.check_output(
        [python_path, '-m', 'unittest', '-v', test_script],
        env=os.environ,
        cwd=dir_path)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
