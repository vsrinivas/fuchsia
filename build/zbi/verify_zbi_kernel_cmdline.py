#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Runs the zbi tool, taking care of unwrapping response files.'''

import argparse
import subprocess
import sys


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
        '--kernel-cmdline-golden-file',
        help='Path to the kernel cmdline golden file',
        required=True)
    parser.add_argument(
        '--stamp', help='Path to the victory file', required=True)
    args = parser.parse_args(input_args)

    if not verify_kernel_cmdline(args.scrutiny, args.zbi_file,
                                 args.kernel_cmdline_golden_file,
                                 args.fuchsia_dir):
        return -1
    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Golden!\n')
    return 0


def verify_kernel_cmdline(
        scrutiny_path, zbi_path, kernel_cmdline_golden_file, fuchsia_dir):
    try:
        cmdline = subprocess.check_output(
            [scrutiny_path, '-c', 'zbi.cmdline --input ' + zbi_path],
            env={
                'FUCHSIA_DIR': fuchsia_dir
            }).decode().strip()
        if cmdline[0] != '"' or cmdline[-1] != '"':
            print('Expect quotes around the scrutiny cmdline output')
            return False
        cmdline = cmdline[1:-1]
    except subprocess.CalledProcessError as e:
        print('Error: Failed to run scrutiny: {0}'.format(e))
        return False
    with open(kernel_cmdline_golden_file, 'r') as f:
        cmdline_golden_file = f.read().strip()
    return compare_cmdline(
        cmdline, cmdline_golden_file, kernel_cmdline_golden_file)


class CmdlineFormatException(Exception):
    """Exception thrown when kernel cmdline is in invalid format."""

    def __init__(self):
        Exception.__init__(self)


def compare_cmdline(actual_cmdline, golden_cmdline, golden_file):
    try:
        golden_entries = parse_cmdline(golden_cmdline)
        actual_entries = parse_cmdline(actual_cmdline)
    except CmdlineFormatException:
        return False
    golden_cmd = generate_sorted_cmdline(golden_entries)
    actual_cmd = generate_sorted_cmdline(actual_entries)
    if golden_cmd != actual_cmd:
        print('Kernel cmdline mismatch!')
        print(
            'Please update kernel cmdline golden file at ' + golden_file +
            ' to:')
        print('```')
        print(actual_cmd)
        print('```')
        print()
        print(
            'To reproduce this error locally, use ' +
            '`fx set --args=dev_fuchsia_zbi_kernel_cmdline_golden=\'"' +
            golden_file.replace('../../', '//') + '"\'`')
        return False
    return True


def parse_cmdline(cmdline):
    cmdline_entries = {}
    entries = cmdline.split(' ')
    for entry in entries:
        key_value = entry.split('=')
        if len(key_value) == 1:
            if key_value[0] != '':
                cmdline_entries[key_value[0]] = True
        elif len(key_value) == 2:
            cmdline_entries[key_value[0]] = key_value[1]
        else:
            print('Error: invalid kernel cmdline, key value pair:' + entry)
            raise CmdlineFormatException

    return cmdline_entries


def generate_sorted_cmdline(entries):
    items = []
    for key in sorted(entries):
        if entries[key] is True:
            items.append(key)
        else:
            items.append(key + '=' + entries[key])
    return ' '.join(items)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
