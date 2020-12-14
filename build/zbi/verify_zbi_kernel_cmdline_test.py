#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Unit test for verify_zbi_kernel_cmdline.py.

Need to have SCRUTINY and ZBI environmental variables set.

To manually run this test:

  SCRUTINY=~/fuchsia/out/default/host_x64/scrutiny \
  ZBI=~/fuchsia/out/default/host_x64/zbi python3 \
  verify_zbi_kernel_cmdline_test.py
"""
import os
import sys
import subprocess
import tempfile
import unittest
import unittest.mock as mock

import verify_zbi_kernel_cmdline


def verify_kernel_cmdline(golden, actual):
    with tempfile.TemporaryDirectory() as test_folder:
        golden_file = os.path.join(test_folder, 'golden')
        stamp_file = os.path.join(test_folder, 'stamp')
        fuchsia_folder = os.path.join(test_folder, 'fuchsia')
        test_zbi = os.path.join(test_folder, 'test.zbi')
        cmdline_file = os.path.join(test_folder, 'cmdline')
        scrutiny = os.environ['SCRUTINY']
        with open(golden_file, 'w+') as f:
            f.write(golden)
        with open(cmdline_file, 'wb+') as f:
            f.write(actual)

        # Use ZBI to create a test.zbi that only contains cmdline.
        subprocess.check_call(
            [os.environ['ZBI'], '-o', test_zbi, '-T', 'CMDLINE', cmdline_file])
        os.mkdir(fuchsia_folder)

        args = [
            '--zbi-file', test_zbi, '--scrutiny', scrutiny, '--fuchsia-dir',
            fuchsia_folder, '--kernel-cmdline-golden-file', golden_file,
            '--stamp', stamp_file
        ]
        # Verify the cmdline in the generated ZBI.
        return verify_zbi_kernel_cmdline.main(args)


class RunVerifyZbiKernelCmdlineTest(unittest.TestCase):

    def test_verify_kernel_cmdline_sucess_normal_case(self):
        self.assertEqual(
            0,
            verify_kernel_cmdline(
                'key1=v1\nkey2=v2\nkey3=v3', b'key1=v1 key2=v2 key3=v3'))

    def test_verify_kernel_cmdline_success_order_diff(self):
        self.assertEqual(
            0,
            verify_kernel_cmdline(
                'key1=v1\nkey2=v2\nkey3=v3', b'key2=v2 key1=v1 key3=v3'))

    def test_verify_kernel_cmdline_success_no_value_option(self):
        self.assertEqual(
            0, verify_kernel_cmdline('option1\noption2', b'option1 option2'))

    def test_verify_kernel_cmdline_fail_golden_empty(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('', b'key2=v2 key1=v1 key3=v3'))

    def test_verify_kernel_cmdline_fail_missing_key2(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('key1=v1\nkey2=v2', b'key1=v1'))

    def test_verify_kernel_cmdline_fail_key1_mismatch(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('key1=v1\nkey2=v2', b'key1=v2 key2=v2'))

    def test_verify_kernel_cmdline_fail_key2_mismatch(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('key1=v1\nkey2=v2', b'key1=v1 key2=v1'))

    def test_verify_kernel_cmdline_fail_additional_key3(self):
        self.assertEqual(
            -1,
            verify_kernel_cmdline(
                'key1=v1\nkey2=v2', b'key1=v1 key2=v2 key3=v3'))

    def test_verify_kernel_cmdline_fail_invalid_format(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('key1=v1\nkey2=v2', b'invalid=format=1'))

    def test_verify_kernel_cmdline_fail_option1_missing(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('option1\noption2', b'option2'))

    def test_verify_kernel_cmdline_fail_additional_option3(self):
        self.assertEqual(
            -1,
            verify_kernel_cmdline(
                'option1\noption2', b'option1 option2 option3'))

    def verify_kernel_cmdline_zbi_not_found():
        with tempfile.TemporaryDirectory() as test_folder:
            golden_file = os.path.join(test_folder, 'golden')
            stamp_file = os.path.join(test_folder, 'stamp')
            fuchsia_folder = os.path.join(test_folder, 'fuchsia')
            test_zbi = os.path.join(test_folder, 'test.zbi')
            scrutiny = os.environ['SCRUTINY']
            with open(golden_file, 'w+') as f:
                f.write('option1')

            # Do not create test_zbi

            os.mkdir(fuchsia_folder)

            args = [
                '--zbi-file', test_zbi, '--scrutiny', scrutiny, '--fuchsia-dir',
                fuchsia_folder, '--kernel-cmdline-golden-file', golden_file,
                '--stamp', stamp_file
            ]
            self.assertEqual(-1, verify_zbi_kernel_cmdline.main(args))


if __name__ == '__main__':
    if 'SCRUTINY' not in os.environ or 'ZBI' not in os.environ:
        print('Please set SCRUTINY and ZBI environmental path')
        sys.exit(-1)
    unittest.main()
