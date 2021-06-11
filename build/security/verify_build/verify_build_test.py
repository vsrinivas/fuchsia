#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
r"""Unit test for verify_build.py.

Need to have SCRUTINY and ZBI environmental variables set.

To manually run this test:

  SCRUTINY=~/fuchsia/out/default/host_x64/scrutiny \
  ZBI=~/fuchsia/out/default/host_x64/zbi python3 \
  verify_build_test.py
"""
import os
import subprocess
import sys
import tempfile
import unittest
import unittest.mock as mock

import verify_build

SUBPROCESS_RUN = subprocess.run


class RunVerifyZbiKernelCmdlineTest(unittest.TestCase):

    def verify_kernel_cmdline(self, golden, actual):
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
                [
                    os.environ['ZBI'], '-o', test_zbi, '-T', 'CMDLINE',
                    cmdline_file
                ])
            os.mkdir(fuchsia_folder)

            args = [
                '--type', 'kernel_cmdline', '--zbi-file', test_zbi,
                '--scrutiny', scrutiny, '--golden-files', golden_file,
                '--stamp', stamp_file
            ]
            # Verify the cmdline in the generated ZBI.
            result = verify_build.main(args)
            if result == 0:
                # Verify stamp file is created.
                self.assertTrue(os.path.isfile(stamp_file))
            return result

    def verify_bootfs_filelist(self, want_filelist, got_files):
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

            # Create a dummy test.zbi. We are not going to use the real scrutiny
            # to parse it so its content doesn't matter.
            with open(test_zbi, 'w+') as f:
                f.write('test ZBI')

            zbi_files = {}
            for file in got_files:
                zbi_files[os.path.join('bootfs', file)] = 'bootfs file'
            fake_subprocess = FakeSubprocess(zbi_files, {})
            with mock.patch('subprocess.run') as mock_run:
                mock_run.side_effect = fake_subprocess.run

                args = [
                    '--type', 'bootfs_filelist', '--zbi-file', test_zbi,
                    '--scrutiny', fake_scrutiny, '--golden-files', golden_file,
                    '--stamp', stamp_file
                ]
                result = verify_build.main(args)
            if result == 0:
                # Verify stamp file is created.
                self.assertTrue(os.path.isfile(stamp_file))
        return result

    def verify_static_pkgs(
            self, want_pkgs, zbi_files, blobfs_files, system_image_files):
        with tempfile.TemporaryDirectory() as test_folder:
            golden_file = os.path.join(test_folder, 'golden')
            stamp_file = os.path.join(test_folder, 'stamp')
            depfile = os.path.join(test_folder, 'depfile')
            fuchsia_folder = os.path.join(test_folder, 'fuchsia')
            os.mkdir(fuchsia_folder)
            test_zbi = os.path.join(test_folder, 'test.zbi')
            test_blobfs = os.path.join(test_folder, 'test.blob')
            with open(golden_file, 'w+') as f:
                f.write(want_pkgs)

            fake_scrutiny = os.path.join(test_folder, 'fake_scrutiny')
            with open(fake_scrutiny, 'w+') as f:
                f.write('fake scrutiny')

            fake_far = os.path.join(test_folder, 'fake_far')
            with open(fake_scrutiny, 'w+') as f:
                f.write('fake far')

            # Create a dummy test.zbi. We are not going to use the real scrutiny
            # to parse it so its content doesn't matter.
            with open(test_zbi, 'w+') as f:
                f.write('test ZBI')

            blobs_folder = os.path.join(test_folder, 'blobs')
            os.mkdir(blobs_folder)
            blobfs_manifest = os.path.join(blobs_folder, 'blobs.manifest')
            with open(blobfs_manifest, 'w+') as bf:
                for blobfs_file in blobfs_files:
                    # We use the blob merkle as the blob content file name here.
                    with open(os.path.join(blobs_folder, blobfs_file),
                              'w+') as f:
                        f.write(blobfs_files[blobfs_file])
                    bf.write(blobfs_file + '=' + blobfs_file + '\n')

            fake_subprocess = FakeSubprocess(zbi_files, system_image_files)
            with mock.patch('subprocess.run') as mock_run:
                mock_run.side_effect = fake_subprocess.run

                args = [
                    '--type',
                    'static_pkgs',
                    '--zbi-file',
                    test_zbi,
                    '--blobfs-manifest',
                    blobfs_manifest,
                    '--scrutiny',
                    fake_scrutiny,
                    '--far',
                    fake_far,
                    '--golden-files',
                    golden_file,
                    '--stamp',
                    stamp_file,
                    '--depfile',
                    depfile,
                ]
                result = verify_build.main(args)

            if result == 0:
                # Verify stamp file is created.
                self.assertTrue(os.path.isfile(stamp_file))
                # Verify depfile is created.
                self.assertTrue(os.path.isfile(depfile))
        return result

    def test_verify_kernel_cmdline_success_normal_case(self):
        self.assertEqual(
            0,
            self.verify_kernel_cmdline(
                'key1=v1\n# comments are ignored\nkey2=v2\nkey3=v3',
                b'key1=v1 key2=v2 key3=v3'))

    def test_verify_kernel_cmdline_success_order_diff(self):
        self.assertEqual(
            0,
            self.verify_kernel_cmdline(
                'key1=v1\nkey2=v2\nkey3=v3', b'key2=v2 key1=v1 key3=v3'))

    def test_verify_kernel_cmdline_success_no_value_option(self):
        self.assertEqual(
            0,
            self.verify_kernel_cmdline('option1\noption2', b'option1 option2'))

    def test_verify_kernel_cmdline_success_transitional(self):
        # ? at start of line marks it optional
        cmdline_golden = 'key1=v1\nkey2=v2\n?key3=v3'
        self.assertEqual(
            0,
            self.verify_kernel_cmdline(
                cmdline_golden, b'key1=v1 key2=v2 key3=v3'))
        self.assertEqual(
            0,
            self.verify_kernel_cmdline(
                cmdline_golden, b'key1=v1 key2=v2'))

    def test_verify_kernel_cmdline_fail_golden_empty(self):
        self.assertEqual(
            1, self.verify_kernel_cmdline('', b'key2=v2 key1=v1 key3=v3'))

    def test_verify_kernel_cmdline_fail_missing_key2(self):
        self.assertEqual(
            1, self.verify_kernel_cmdline('key1=v1\nkey2=v2', b'key1=v1'))

    def test_verify_kernel_cmdline_fail_key1_mismatch(self):
        self.assertEqual(
            1,
            self.verify_kernel_cmdline('key1=v1\nkey2=v2', b'key1=v2 key2=v2'))

    def test_verify_kernel_cmdline_fail_key2_mismatch(self):
        self.assertEqual(
            1,
            self.verify_kernel_cmdline('key1=v1\nkey2=v2', b'key1=v1 key2=v1'))

    def test_verify_kernel_cmdline_fail_additional_key3(self):
        self.assertEqual(
            1,
            self.verify_kernel_cmdline(
                'key1=v1\nkey2=v2', b'key1=v1 key2=v2 key3=v3'))

    def test_verify_kernel_cmdline_fail_invalid_format(self):
        self.assertEqual(
            1,
            self.verify_kernel_cmdline('key1=v1\nkey2=v2', b'invalid=format=1'))

    def test_verify_kernel_cmdline_fail_option1_missing(self):
        self.assertEqual(
            1, self.verify_kernel_cmdline('option1\noption2', b'option2'))

    def test_verify_kernel_cmdline_fail_additional_option3(self):
        self.assertEqual(
            1,
            self.verify_kernel_cmdline(
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
                '--scrutiny', scrutiny, '--golden-files', golden_file,
                '--stamp', stamp_file
            ]
            self.assertEqual(1, verify_build.main(args))

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
                '--scrutiny', scrutiny, '--golden-files', golden_file,
                '--stamp', stamp_file
            ]
            self.assertEqual(0, verify_build.main(args))

    def test_verify_kernel_cmdline_fail_golden_empty_cmdline_found(self):
        self.assertEqual(1, self.verify_kernel_cmdline('', b'option2'))

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
                '--scrutiny', scrutiny, '--golden-files', golden_file,
                '--stamp', stamp_file
            ]
            self.assertEqual(1, verify_build.main(args))

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
                '--scrutiny', scrutiny, '--golden-files', golden_file_1,
                golden_file_2, '--stamp', stamp_file
            ]
            self.assertEqual(1, verify_build.main(args))

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
                '--scrutiny', scrutiny, '--golden-files', golden_file_1,
                golden_file_2, golden_file_3, '--stamp', stamp_file
            ]
            # We do not support more than two golden files.
            self.assertEqual(0, verify_build.main(args))

    def test_verify_bootfs_filelist_normal_case(self):
        self.assertEqual(
            0,
            self.verify_bootfs_filelist(
                'fileA\nfileB\n# comments are ignored\nfileC',
                ['fileA', 'fileC', 'fileB']))

    def test_verify_bootfs_filelist_sub_dir(self):
        self.assertEqual(
            0,
            self.verify_bootfs_filelist(
                'dir/fileA\ndir/fileC\nfileB',
                ['dir/fileA', 'dir/fileC', 'fileB']))

    def test_verify_bootfs_filelist_mismatch(self):
        self.assertEqual(
            1,
            self.verify_bootfs_filelist(
                'fileA\nfileB\nfileC', ['fileA', 'fileC']))

    def test_verify_bootfs_filelist_sub_dir_mismatch(self):
        self.assertEqual(
            1,
            self.verify_bootfs_filelist(
                'dir/fileA\ndir/fileC\nfileB',
                ['dir1/fileA', 'dir/fileC', 'fileB']))

    def test_verify_bootfs_filelist_transitional(self):
        # ? at start of line permits presence or absence of file for soft
        # transitions
        golden_contents = 'fileA\n?fileB\nfileC'
        self.assertEqual(
            0,
            self.verify_bootfs_filelist(
                golden_contents,
                ['fileA', 'fileB', 'fileC']))
        self.assertEqual(
            0,
            self.verify_bootfs_filelist(golden_contents, ['fileA', 'fileC']))

    def test_verify_static_pkgs_normal_case(self):
        static_packages = 'pkg0/0=1\npkg1/0=1\npkg2/0=2'
        zbi_files = {
            'bootfs/config/devmgr': 'zircon.system.pkgfs.cmd=bin/pkgsvr+1234'
        }
        blobfs_files = {'1234': 'system_image', '2345': static_packages}
        system_image_files = {'meta/contents': 'data/static_packages=2345'}
        self.assertEqual(
            0,
            self.verify_static_pkgs(
                '# allow comments\npkg0\npkg1\npkg2', zbi_files, blobfs_files,
                system_image_files))

    def test_verify_static_pkgs_order(self):
        static_packages = 'pkg2/2=1\npkg1/1=1\npkg0/0=2'
        zbi_files = {
            'bootfs/config/devmgr': 'zircon.system.pkgfs.cmd=bin/pkgsvr+1234'
        }
        blobfs_files = {'1234': 'system_image', '2345': static_packages}
        system_image_files = {'meta/contents': 'data/static_packages=2345'}
        self.assertEqual(
            0,
            self.verify_static_pkgs(
                'pkg0\npkg1\npkg2', zbi_files, blobfs_files,
                system_image_files))

    def test_verify_static_pkgs_transitional(self):
        static_packages_with_pkg2 = 'pkg0/0=1\npkg1/0=1\npkg2/0=2'
        static_packages_without_pkg2 = 'pkg0/0=1\npkg1/0=1\npkg2/0=2'
        zbi_files = {
            'bootfs/config/devmgr': 'zircon.system.pkgfs.cmd=bin/pkgsvr+1234'
        }
        blobfs_files_with_pkg2 = {'1234': 'system_image',
                                  '2345': static_packages_with_pkg2}
        blobfs_files_without_pkg2 = {'1234': 'system_image',
                                     '2345': static_packages_without_pkg2}
        system_image_files = {'meta/contents': 'data/static_packages=2345'}
        self.assertEqual(
            0,
            self.verify_static_pkgs(
                'pkg0\npkg1\n?pkg2', zbi_files, blobfs_files_with_pkg2,
                system_image_files))

        self.assertEqual(
            0,
            self.verify_static_pkgs(
                'pkg0\npkg1\n?pkg2', zbi_files, blobfs_files_without_pkg2,
                system_image_files))

    def test_verify_static_pkgs_mismatch(self):
        static_packages = 'pkg0/0=1\npkg1/0=1'
        zbi_files = {
            'bootfs/config/devmgr': 'zircon.system.pkgfs.cmd=bin/pkgsvr+1234'
        }
        blobfs_files = {'1234': 'system_image', '2345': static_packages}
        system_image_files = {'meta/contents': 'data/static_packages=2345'}
        self.assertEqual(
            1,
            self.verify_static_pkgs(
                'pkg0\npkg1\npkg2', zbi_files, blobfs_files,
                system_image_files))

    def test_verify_static_pkgs_no_devmgr_config(self):
        static_packages = 'pkg0/0=1\npkg1/0=1\npkg2/0=2'
        zbi_files = {}
        blobfs_files = {'1234': 'system_image', '2345': static_packages}
        system_image_files = {'meta/contents': 'data/static_packages=2345'}
        self.assertEqual(
            1,
            self.verify_static_pkgs(
                'pkg0\npkg1\npkg2', zbi_files, blobfs_files,
                system_image_files))

    def test_verify_static_pkgs_invalid_devmgr_config(self):
        static_packages = 'pkg0/0=1\npkg1/0=1\npkg2/0=2'
        zbi_files = {'bootfs/config/devmgr': 'zircon.system.pkgfs.cmd'}
        blobfs_files = {'1234': 'system_image', '2345': static_packages}
        system_image_files = {'meta/contents': 'data/static_packages=2345'}
        self.assertEqual(
            1,
            self.verify_static_pkgs(
                'pkg0\npkg1\npkg2', zbi_files, blobfs_files,
                system_image_files))

    def test_verify_static_pkgs_system_image_blob_not_found(self):
        static_packages = 'pkg0/0=1\npkg1/0=1\npkg2/0=2'
        zbi_files = {
            'bootfs/config/devmgr': 'zircon.system.pkgfs.cmd=bin/pkgsvr+1234'
        }
        blobfs_files = {'2345': static_packages}
        system_image_files = {'meta/contents': 'data/static_packages=2345'}
        self.assertEqual(
            1,
            self.verify_static_pkgs(
                'pkg0\npkg1\npkg2', zbi_files, blobfs_files,
                system_image_files))

    def test_verify_static_pkgs_invalid_system_image(self):
        static_packages = 'pkg0/0=1\npkg1/0=1\npkg2/0=2'
        zbi_files = {
            'bootfs/config/devmgr': 'zircon.system.pkgfs.cmd=bin/pkgsvr+1234'
        }
        blobfs_files = {'1234': 'system_image', '2345': static_packages}
        system_image_files = {}
        self.assertEqual(
            1,
            self.verify_static_pkgs(
                'pkg0\npkg1\npkg2', zbi_files, blobfs_files,
                system_image_files))

    def test_verify_static_pkgs_static_pkgs_blob_not_found(self):
        zbi_files = {
            'bootfs/config/devmgr': 'zircon.system.pkgfs.cmd=bin/pkgsvr+1234'
        }
        blobfs_files = {'1234': 'system_image'}
        system_image_files = {'meta/contents': 'data/static_packages=2345'}
        self.assertEqual(
            1,
            self.verify_static_pkgs(
                'pkg0\npkg1\npkg2', zbi_files, blobfs_files,
                system_image_files))

    def test_verify_static_pkgs_invalid_static_pkgs_list(self):
        static_packages = 'pkg0/0'
        zbi_files = {
            'bootfs/config/devmgr': 'zircon.system.pkgfs.cmd=bin/pkgsvr+1234'
        }
        blobfs_files = {'1234': 'system_image', '2345': static_packages}
        system_image_files = {'meta/contents': 'data/static_packages=2345'}
        self.assertEqual(
            1,
            self.verify_static_pkgs(
                'pkg0\npkg1\npkg2', zbi_files, blobfs_files,
                system_image_files))


class FakeSubprocess(object):

    def __init__(self, zbi_files, system_image_files):
        self.zbi_files = zbi_files
        self.system_image_files = system_image_files

    def _write_files(files, output):
        for file in files:
            dirpath = os.path.dirname(os.path.join(output, file))
            if not os.path.exists(dirpath):
                os.makedirs(dirpath, exist_ok=True)
            with open(os.path.join(output, file), 'w+') as f:
                f.write(files[file])

    def run(self, *argv, **kwargs):
        del kwargs
        command = argv[0]
        if command[0].endswith('fake_scrutiny'):
            output = ''
            input = ''
            scrutiny_commands = command[2].split(' ')
            for i in range(0, len(scrutiny_commands) - 1):
                if scrutiny_commands[i] == '--output':
                    output = scrutiny_commands[i + 1]
                if scrutiny_commands[i] == '--input':
                    input = scrutiny_commands[i + 1]

            if not os.path.exists(input):
                raise subprocess.CalledProcessError(
                    cmd=command,
                    returncode=1,
                    stderr=('input: ' + input + ' not found').encode())
            op = scrutiny_commands[0]
            if op == 'tool.zbi.extract':
                FakeSubprocess._write_files(self.zbi_files, output)
            else:
                raise subprocess.CalledProcessError(
                    cmd=command,
                    returncode=1,
                    stderr=('unknown scrutiny command: ' + op).encode())

            return subprocess.CompletedProcess(
                args=[], returncode=0, stdout=b'{"status":"ok"}')
        elif command[0].endswith('fake_far'):
            input = (command[2].split('='))[1]
            if not os.path.exists(input):
                raise subprocess.CalledProcessError(
                    cmd=command,
                    returncode=1,
                    stderr=('input: ' + input + ' not found').encode())
            output = (command[3].split('='))[1]
            os.mkdir(output)
            FakeSubprocess._write_files(self.system_image_files, output)
            return subprocess.CompletedProcess(
                args=[], returncode=0, stdout=b'')

        raise subprocess.CalledProcessError(
            cmd=command, returncode=1, stderr=b'unsupported command')


if __name__ == '__main__':
    if 'SCRUTINY' not in os.environ or 'ZBI' not in os.environ:
        print('Please set SCRUTINY and ZBI environmental path')
        sys.exit(1)
    unittest.main()
