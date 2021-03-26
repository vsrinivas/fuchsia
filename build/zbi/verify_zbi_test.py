#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
r"""Unit test for verify_zbi.py.

Need to have SCRUTINY and ZBI environmental variables set.

To manually run this test:

  SCRUTINY=~/fuchsia/out/default/host_x64/scrutiny \
  ZBI=~/fuchsia/out/default/host_x64/zbi python3 \
  verify_zbi_test.py
"""
import os
import subprocess
import sys
import tempfile
import unittest
import unittest.mock as mock

import verify_zbi

SUBPROCESS_RUN = subprocess.run


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
            '--type', 'kernel_cmdline', '--zbi-file', test_zbi, '--scrutiny',
            scrutiny, '--fuchsia-dir', fuchsia_folder, '--golden-files',
            golden_file, '--stamp', stamp_file
        ]
        # Verify the cmdline in the generated ZBI.
        return verify_zbi.main(args)


def verify_bootfs_filelist(want_filelist, got_files):
    with tempfile.TemporaryDirectory() as test_folder:
        golden_file = os.path.join(test_folder, 'golden')
        stamp_file = os.path.join(test_folder, 'stamp')
        fuchsia_folder = os.path.join(test_folder, 'fuchsia')
        os.mkdir(fuchsia_folder)
        test_zbi = os.path.join(test_folder, 'test.zbi')
        with open(golden_file, 'w+') as f:
            f.write(want_filelist)

        fake_scrutiny = os.path.join(test_folder, 'fake_scrutiny')
        with open(fake_scrutiny, 'w+') as f:
            f.write('fake scrutiny')

        # Create a dummy test.zbi. We are not going to use the real scrutiny to
        # parse it so its content doesn't matter.
        with open(test_zbi, 'w+') as f:
            f.write('test ZBI')

        fake_subprocess = FakeSubprocess(got_files, [])
        with mock.patch('subprocess.check_call') as mock_run:
            mock_run.side_effect = fake_subprocess.run

            args = [
                '--type', 'bootfs_filelist', '--zbi-file', test_zbi,
                '--scrutiny', fake_scrutiny, '--fuchsia-dir', fuchsia_folder,
                '--golden-files', golden_file, '--stamp', stamp_file
            ]
            return verify_zbi.main(args)


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
            1, verify_kernel_cmdline('', b'key2=v2 key1=v1 key3=v3'))

    def test_verify_kernel_cmdline_fail_missing_key2(self):
        self.assertEqual(
            1, verify_kernel_cmdline('key1=v1\nkey2=v2', b'key1=v1'))

    def test_verify_kernel_cmdline_fail_key1_mismatch(self):
        self.assertEqual(
            1, verify_kernel_cmdline('key1=v1\nkey2=v2', b'key1=v2 key2=v2'))

    def test_verify_kernel_cmdline_fail_key2_mismatch(self):
        self.assertEqual(
            1, verify_kernel_cmdline('key1=v1\nkey2=v2', b'key1=v1 key2=v1'))

    def test_verify_kernel_cmdline_fail_additional_key3(self):
        self.assertEqual(
            1,
            verify_kernel_cmdline(
                'key1=v1\nkey2=v2', b'key1=v1 key2=v2 key3=v3'))

    def test_verify_kernel_cmdline_fail_invalid_format(self):
        self.assertEqual(
            1, verify_kernel_cmdline('key1=v1\nkey2=v2', b'invalid=format=1'))

    def test_verify_kernel_cmdline_fail_option1_missing(self):
        self.assertEqual(
            1, verify_kernel_cmdline('option1\noption2', b'option2'))

    def test_verify_kernel_cmdline_fail_additional_option3(self):
        self.assertEqual(
            1,
            verify_kernel_cmdline(
                'option1\noption2', b'option1 option2 option3'))

    def test_verify_kernel_cmdline_zbi_not_found(self):
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
                '--type', 'kernel_cmdline', '--zbi-file', test_zbi,
                '--scrutiny', scrutiny, '--fuchsia-dir', fuchsia_folder,
                '--golden-files', golden_file, '--stamp', stamp_file
            ]
            self.assertEqual(1, verify_zbi.main(args))

    def test_verify_kernel_cmdline_success_no_cmdline_found(self):
        with tempfile.TemporaryDirectory() as test_folder:
            golden_file = os.path.join(test_folder, 'golden')
            stamp_file = os.path.join(test_folder, 'stamp')
            fuchsia_folder = os.path.join(test_folder, 'fuchsia')
            test_zbi = os.path.join(test_folder, 'test.zbi')
            scrutiny = os.environ['SCRUTINY']

            # Create an empty golden file
            with open(golden_file, 'w+') as f:
                f.write('')

            # Use ZBI to create a test.zbi with no cmdline.
            subprocess.check_call([os.environ['ZBI'], '-o', test_zbi])

            os.mkdir(fuchsia_folder)

            args = [
                '--type', 'kernel_cmdline', '--zbi-file', test_zbi,
                '--scrutiny', scrutiny, '--fuchsia-dir', fuchsia_folder,
                '--golden-files', golden_file, '--stamp', stamp_file
            ]
            self.assertEqual(0, verify_zbi.main(args))

    def test_verify_kernel_cmdline_fail_golden_empty_cmdline_found(self):
        self.assertEqual(1, verify_kernel_cmdline('', b'option2'))

    def test_verify_kernel_cmdline_fail_golden_not_empty_cmdline_not_found(
            self):
        with tempfile.TemporaryDirectory() as test_folder:
            golden_file = os.path.join(test_folder, 'golden')
            stamp_file = os.path.join(test_folder, 'stamp')
            fuchsia_folder = os.path.join(test_folder, 'fuchsia')
            test_zbi = os.path.join(test_folder, 'test.zbi')
            scrutiny = os.environ['SCRUTINY']

            # Create an empty golden file
            with open(golden_file, 'w+') as f:
                f.write('option1')

            # Use ZBI to create a test.zbi with no cmdline.
            subprocess.check_call([os.environ['ZBI'], '-o', test_zbi])

            os.mkdir(fuchsia_folder)

            args = [
                '--type', 'kernel_cmdline', '--zbi-file', test_zbi,
                '--scrutiny', scrutiny, '--fuchsia-dir', fuchsia_folder,
                '--golden-files', golden_file, '--stamp', stamp_file
            ]
            self.assertEqual(1, verify_zbi.main(args))

    def test_verify_kernel_cmdline_multiple_golden_files_one_match(self):
        with tempfile.TemporaryDirectory() as test_folder:
            golden_file_1 = os.path.join(test_folder, 'golden_1')
            golden_file_2 = os.path.join(test_folder, 'golden_2')
            stamp_file = os.path.join(test_folder, 'stamp')
            fuchsia_folder = os.path.join(test_folder, 'fuchsia')
            test_zbi = os.path.join(test_folder, 'test.zbi')
            scrutiny = os.environ['SCRUTINY']
            cmdline_file = os.path.join(test_folder, 'cmdline')

            # golden_file_1 does not match.
            with open(golden_file_1, 'w+') as f:
                f.write('option1')

            # golden_file_2 matches.
            with open(golden_file_2, 'w+') as f:
                f.write('option1 option2')

            with open(cmdline_file, 'wb+') as f:
                f.write(b'option1 option2')

            # Use ZBI to create a test.zbi that only contains cmdline.
            subprocess.check_call(
                [
                    os.environ['ZBI'], '-o', test_zbi, '-T', 'CMDLINE',
                    cmdline_file
                ])

            os.mkdir(fuchsia_folder)

            args = [
                '--type', 'kernel_cmdline', '--zbi-file', test_zbi,
                '--scrutiny', scrutiny, '--fuchsia-dir', fuchsia_folder,
                '--golden-files', golden_file_1, golden_file_2, '--stamp',
                stamp_file
            ]
            self.assertEqual(1, verify_zbi.main(args))

    def test_verify_kernel_cmdline_three_golden_files_not_supported(self):
        with tempfile.TemporaryDirectory() as test_folder:
            golden_file_1 = os.path.join(test_folder, 'golden_1')
            golden_file_2 = os.path.join(test_folder, 'golden_2')
            golden_file_3 = os.path.join(test_folder, 'golden_3')
            stamp_file = os.path.join(test_folder, 'stamp')
            fuchsia_folder = os.path.join(test_folder, 'fuchsia')
            test_zbi = os.path.join(test_folder, 'test.zbi')
            scrutiny = os.environ['SCRUTINY']
            cmdline_file = os.path.join(test_folder, 'cmdline')

            with open(golden_file_1, 'w+') as f:
                f.write('option1')
            with open(golden_file_2, 'w+') as f:
                f.write('option1')
            with open(golden_file_3, 'w+') as f:
                f.write('option1')

            with open(cmdline_file, 'wb+') as f:
                f.write(b'option1')

            # Use ZBI to create a test.zbi that only contains cmdline.
            subprocess.check_call(
                [
                    os.environ['ZBI'], '-o', test_zbi, '-T', 'CMDLINE',
                    cmdline_file
                ])

            os.mkdir(fuchsia_folder)

            args = [
                '--type', 'kernel_cmdline', '--zbi-file', test_zbi,
                '--scrutiny', scrutiny, '--fuchsia-dir', fuchsia_folder,
                '--golden-files', golden_file_1, golden_file_2, golden_file_3,
                '--stamp', stamp_file
            ]
            # We do not support more than two golden files.
            self.assertEqual(0, verify_zbi.main(args))

    def test_verify_bootfs_filelist_normal_case(self):
        self.assertEqual(
            0,
            verify_bootfs_filelist(
                'fileA\nfileB\nfileC', ['fileA', 'fileC', 'fileB']))

    def test_verify_bootfs_filelist_sub_dir(self):
        self.assertEqual(
            0,
            verify_bootfs_filelist(
                'dir/fileA\ndir/fileC\nfileB',
                ['dir/fileA', 'dir/fileC', 'fileB']))

    def test_verify_bootfs_filelist_mismatch(self):
        self.assertEqual(
            1,
            verify_bootfs_filelist('fileA\nfileB\nfileC', ['fileA', 'fileC']))

    def test_verify_bootfs_filelist_sub_dir_mismatch(self):
        self.assertEqual(
            1,
            verify_bootfs_filelist(
                'dir/fileA\ndir/fileC\nfileB',
                ['dir1/fileA', 'dir/fileC', 'fileB']))


class FakeSubprocess(object):

    def __init__(self, bootfs_filelist, static_pkgs_list):
        self.bootfs_filelist = bootfs_filelist
        self.static_pkgs_list = static_pkgs_list

    def run(self, *argv, **kwargs):
        del kwargs
        command = argv[0]
        print(command[0])
        if command[0].endswith('fake_scrutiny'):
            output = ''
            scrutiny_commands = command[2].split(' ')
            for i in range(0, len(scrutiny_commands) - 1):
                if scrutiny_commands[i] == '--output':
                    output = scrutiny_commands[i + 1]
            os.mkdir(os.path.join(output, 'bootfs'))
            for file in self.bootfs_filelist:
                dirpath = os.path.dirname(os.path.join(output, 'bootfs', file))
                if not os.path.exists(dirpath):
                    os.mkdir(dirpath)
                with open(os.path.join(output, 'bootfs', file), 'w+') as f:
                    f.write('bootfs file')

        return subprocess.CompletedProcess(args=[], returncode=0, stdout=b'')


if __name__ == '__main__':
    if 'SCRUTINY' not in os.environ or 'ZBI' not in os.environ:
        print('Please set SCRUTINY and ZBI environmental path')
        sys.exit(1)
    unittest.main()
