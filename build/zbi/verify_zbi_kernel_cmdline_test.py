#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Unit test for verify_zbi_kernel_cmdline.py.

To manually run this test:
  python3 verify_zbi_kernel_cmdline_test.py
"""
import os
import tempfile
import unittest
import unittest.mock as mock

import verify_zbi_kernel_cmdline


def verify_kernel_cmdline(golden, actual):
    with tempfile.TemporaryDirectory() as test_folder:
        golden_file = os.path.join(test_folder, 'golden')
        stamp_file = os.path.join(test_folder, 'stamp')
        with open(golden_file, 'w+') as f:
            f.write(golden)

        args = [
            '--zbi-file', 'test.zbi', '--scrutiny', 'mock_scrutiny',
            '--fuchsia-dir', '/fuchisa', '--kernel-cmdline-golden-file',
            golden_file, '--stamp', stamp_file
        ]
        with mock.patch('subprocess.check_output') as mock_output:
            mock_output.return_value = actual
            result = verify_zbi_kernel_cmdline.main(args)
            mock_output.assert_called_once()
            return result


class RunVerifyZbiKernelCmdlineTest(unittest.TestCase):

    def test_verify_kernel_cmdline_sucess_normal_case(self):
        self.assertEqual(
            0,
            verify_kernel_cmdline(
                'key1=v1 key2=v2 key3=v3', b'"key1=v1 key2=v2 key3=v3"\n'))

    def test_verify_kernel_cmdline_success_order_diff(self):
        self.assertEqual(
            0,
            verify_kernel_cmdline(
                'key1=v1 key2=v2 key3=v3', b'"key2=v2 key1=v1 key3=v3"\n'))

    def test_verify_kernel_cmdline_success_no_value_option(self):
        self.assertEqual(
            0, verify_kernel_cmdline('option1 option2', b'"option1 option2"\n'))

    def test_verify_kernel_cmdline_fail_golden_empty(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('', b'"key2=v2 key1=v1 key3=v3"\n'))

    def test_verify_kernel_cmdline_fail_missing_key2(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('key1=v1 key2=v2', b'"key1=v1"\n'))

    def test_verify_kernel_cmdline_fail_key1_mismatch(self):
        self.assertEqual(
            -1,
            verify_kernel_cmdline('key1=v1 key2=v2', b'"key1=v2 key2=v2"\n'))

    def test_verify_kernel_cmdline_fail_key2_mismatch(self):
        self.assertEqual(
            -1,
            verify_kernel_cmdline('key1=v1 key2=v2', b'"key1=v1 key2=v1"\n'))

    def test_verify_kernel_cmdline_fail_additional_key3(self):
        self.assertEqual(
            -1,
            verify_kernel_cmdline(
                'key1=v1 key2=v2', b'"key1=v1 key2=v2 key3=v3"\n'))

    def test_verify_kernel_cmdline_fail_invalid_format(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('key1=v1 key2=v2', b'"invalid=format=1"'))

    def test_verify_kernel_cmdline_fail_option1_missing(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('option1 option2', b'"option2"\n'))

    def test_verify_kernel_cmdline_fail_additional_option3(self):
        self.assertEqual(
            -1,
            verify_kernel_cmdline(
                'option1 option2', b'"option1 option2 option3"\n'))

    def test_verify_kernel_cmdline_fail_missing_quotes(self):
        self.assertEqual(
            -1, verify_kernel_cmdline('option1 option2', b'option2 option3"\n'))


if __name__ == '__main__':
    unittest.main()
