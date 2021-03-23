#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Use scrutiny to verify various ZBI items."""

import argparse
import difflib
import os
import subprocess
import sys
import shlex
import tempfile

SUPPORTED_TYPES = ['kernel_cmdline', 'bootfs_filelist']


def print_error(msg):
    print(msg, file=sys.stderr)


def main(input_args):
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--zbi-file', help='Path to the zbi to verify', required=True)
    parser.add_argument(
        '--scrutiny',
        help='Path to the scrutiny tool used for verifying kernel cmdline',
        required=True)
    parser.add_argument(
        '--fuchsia-dir',
        help='Path to fuchsia root directory, required for scrutiny to work',
        required=True)
    parser.add_argument(
        '--golden-file', help='Path to the golden file', required=True)
    parser.add_argument(
        '--stamp', help='Path to the victory file', required=True)
    parser.add_argument(
        '--type',
        help=('The type of the ZBI item to verify'),
        choices=SUPPORTED_TYPES,
        required=True)
    args = parser.parse_args(input_args)

    try:
        if not verify_zbi(args):
            return 1
    except (AssertionError, IOError) as e:
        print_error(str(e))
        return 1
    except CmdlineFormatError as e:
        print_error('kernel cmdline is not formatted correctly: {}'.format(e))
        return 1
    except subprocess.CalledProcessError as e:
        print_error('failed to run scrutiny: {}'.format(e))
        return 1
    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Golden!\n')
    return 0


def verify_zbi(args):
    """verify_zbi verifies the ZBI image and return True if verified.

    Raises:
      AssertionError: If verification fails.
      IOError: If verification fails.
      subprocess.CalledProcessError: If failed to run scrutiny.
      CmdlineFormatError: If the kernel cmdline is not formatted correctly.
    """
    # Check for some necessary files/dirs exist first.
    for file in [args.scrutiny, args.zbi_file, args.fuchsia_dir]:
        assert os.path.exists(file)

    with tempfile.TemporaryDirectory() as tmp:
        subprocess.check_call(
            [
                args.scrutiny, '-c', 'tool.zbi.extract --input ' +
                shlex.quote(args.zbi_file) + ' --output ' + shlex.quote(tmp)
            ],
            env={'FUCHSIA_DIR': args.fuchsia_dir})

        if args.type == 'kernel_cmdline':
            return verify_kernel_cmdline(args.golden_file, tmp)
        elif args.type == 'bootfs_filelist':
            return verify_bootfs_filelist(args.golden_file, tmp)

        return True


def verify_kernel_cmdline(kernel_cmdline_golden_file, scrutiny_out):
    """verify_zbi verifies the kernel cmdline in ZBI image.

    Raises:
      IOError: If verification fails.
      CmdlineFormatError: If the kernel cmdline is not formatted correctly.
    """
    with open(kernel_cmdline_golden_file, 'r') as f:
        golden_file_content = f.read().strip()
    if not os.path.exists(os.path.join(scrutiny_out, 'sections',
                                       'cmdline.blk')):
        # We find no kernel cmdline. Check whether the golden file is empty.
        if not golden_file_content:
            # Golden file is empty. Pass the check.
            return True
        else:
            print_error('Found no kernel cmdline in ZBI')
            print_error(
                'Please update kernel cmdline golden file at ' +
                golden_file_content + ' to be an empty file')
            return False
    with open(os.path.join(scrutiny_out, 'sections', 'cmdline.blk'), 'r') as f:
        # The cmdline.blk contains a trailing \x00.
        cmdline = f.read().strip().rstrip('\x00')
    return compare_cmdline(
        cmdline, golden_file_content, kernel_cmdline_golden_file)


def verify_bootfs_filelist(bootfs_filelist_golden_file, scrutiny_out):
    """verify_zbi verifies the bootFS filelist in ZBI image.

    Raises:
      IOError: If verification fails.
    """
    with open(bootfs_filelist_golden_file, 'r') as f:
        golden_file_content = f.read().strip()
    bootfs_folder = os.path.join(scrutiny_out, 'bootfs')
    bootfs_files = []
    for root, _, files in os.walk(bootfs_folder):
        for file in files:
            bootfs_files.append(
                os.path.relpath(os.path.join(root, file), bootfs_folder))
    got_content = '\n'.join(sorted(bootfs_files))

    if golden_file_content != got_content:
        print_error('BootFS file list mismatch!')
        print_error(
            'Please update bootFS file list golden file at ' +
            bootfs_filelist_golden_file + ' to:')
        print_error('```')
        print_error(got_content)
        print_error('```')
        print_error('')
        print_error('Diff:')
        sys.stderr.writelines(
            difflib.context_diff(
                golden_file_content.splitlines(keepends=True),
                got_content.splitlines(keepends=True),
                fromfile='want',
                tofile='got'))
        return False
    return True


class CmdlineFormatError(Exception):
    """Exception thrown when kernel cmdline is in invalid format."""

    def __init__(self, msg):
        Exception.__init__(self)
        self.msg = msg

    def __str__(self):
        return self.msg


def compare_cmdline(actual_cmdline, golden_cmdline, golden_file):
    """compare_cmdline compares the actual cmdline with the golden cmdline.

    Raises:
      CmdlineFormatError: If the kernel cmdline is not formatted correctly.
    """
    golden_cmd = generate_sorted_cmdline(golden_cmdline, '\n')
    actual_cmd = generate_sorted_cmdline(actual_cmdline, ' ')
    if golden_cmd != actual_cmd:
        print_error('Kernel cmdline mismatch!')
        print_error(
            'Please update kernel cmdline golden file at ' + golden_file +
            ' to:')
        print_error('```')
        print_error(actual_cmd)
        print_error('```')
        print_error('')
        print_error('Diff:')
        print_error(difflib.context_diff(golden_cmd, actual_cmd))
        return False
    return True


def generate_sorted_cmdline(cmdline, splitter):
    """generate_sorted_cmdline generates a kernel cmdline sorted by entry keys.

    Raises:
      CmdlineFormatError: If the kernel cmdline is not formatted correctly.
    """
    cmdline_entries = {}
    entries = cmdline.split(splitter)
    for entry in entries:
        if len(entry.split('=')) > 2:
            raise CmdlineFormatError(
                'invalid kernel cmdline, key value pair: ' + entry)
        key, _, value = entry.partition('=')
        if key in cmdline_entries:
            raise CmdlineFormatError('duplicate kernel cmdline key: ' + key)
        cmdline_entries[key] = value

    return '\n'.join(
        ('%s=%s' % (key, value)) if value else key
        for key, value in sorted(cmdline_entries.items()))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
