#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Runs the GN SDK tests
usage: run.py [-h] [--proj_dir PROJ_DIR] [--out_dir OUT_DIR]

optional arguments:
  -h, --help           show this help message and exit
  --proj_dir PROJ_DIR  Path to the test project directory
  --out_dir OUT_DIR    Path to the out directory

PROJ_DIR defaults to the same directory run.py is contained in
OUT_DIR defaults to ./out/default relative to the run.py
"""

import imp
import os
from subprocess import Popen
import sys
import unittest

FUCHSIA_ROOT = '${data.fuchsia_root}'

# Import gen_fidl_response_file_unittest
GEN_FIDL_RESP_FILE_TEST_PATH = os.path.join(
    FUCHSIA_ROOT, 'scripts', 'sdk', 'gn', 'gen_fidl_response_file_unittest.py')
gen_fidl_response_file_unittest = imp.load_source(
    'gen_fidl_response_file_unittest', GEN_FIDL_RESP_FILE_TEST_PATH)

# bash_tests constants
BASH_TESTS_PATH = os.path.join(
    FUCHSIA_ROOT, 'scripts', 'sdk', 'gn', 'bash_tests', 'run_bash_tests.sh')


class GnTester(object):
    """Class for GN SDK test setup, execution, and cleanup."""

    def __init__(self):
        self._test_failed = False

    def _run_unit_test(self, test_module):
        loader = unittest.TestLoader()
        tests = loader.loadTestsFromModule(test_module)
        suite = unittest.TestSuite()
        suite.addTests(tests)
        runner = unittest.TextTestRunner()
        result = runner.run(suite)
        if result.failures or result.errors:
            raise AssertionError('Unit test failed.')

    def _gen_fild_resp_file_unittest(self):
        self._run_unit_test(gen_fidl_response_file_unittest)

    def _bash_tests(self):
        job = Popen(BASH_TESTS_PATH)
        job.communicate()
        if job.returncode:
            msg = 'Bash tests returned non-zero exit code: %s' % job.returncode
            raise AssertionError(msg)

    def _run_test(self, test):
        try:
            getattr(self, test)()
        except:
            self._test_failed = True

    def run(self):
        self._run_test("_gen_fild_resp_file_unittest")
        self._run_test("_bash_tests")
        return self._test_failed


def main():
    if GnTester().run():
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
