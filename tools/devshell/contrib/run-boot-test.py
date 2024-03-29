#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import platform
import shlex
import subprocess
import sys


def find_bootserver(build_dir):
    host_os = {'Linux': 'linux', 'Darwin': 'mac'}[platform.system()]
    host_cpu = {'x86_64': 'x64', 'arm64': 'arm64'}[platform.machine()]
    with open(os.path.join(build_dir, 'tool_paths.json')) as file:
        tool_paths = json.load(file)
    bootservers = [
        os.path.join(build_dir, tool['path']) for tool in tool_paths if (
            tool['name'] == 'bootserver' and tool['cpu'] == host_cpu and
            tool['os'] == host_os)
    ]
    if bootservers:
        return bootservers[0]
    print('Cannot find bootserver for %s-%s' % (host_os, host_cpu))
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        prog='fx run-boot-test', description='Run a boot test.')
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument(
        '--boot', '-b', action='store_true', help='Run via bootserver')
    parser.add_argument(
        '--args',
        '-a',
        metavar='RUNNER-ARG',
        action='append',
        default=[],
        help='Pass RUNNER-ARG to bootserver/fx qemu')
    parser.add_argument(
        '-k',
        action='append_const',
        dest='args',
        const='-k',
        help='Shorthand for --args=-k')
    parser.add_argument(
        '--cmdline',
        '-c',
        metavar='KERNEL-ARGS',
        action='append',
        default=[],
        help='Add kernel command-line arguments.')
    parser.add_argument(
        'name', help='Name of the zbi_test()/qemu_kernel_test()/efi_test() target to run', nargs='?')
    args = parser.parse_args()

    build_dir = os.path.relpath(os.getenv('FUCHSIA_BUILD_DIR'))
    if build_dir is None:
        print('FUCHSIA_BUILD_DIR not set')
        return 1
    test_cpu = os.getenv('FUCHSIA_ARCH')
    if test_cpu is None:
        print('FUCHSIA_ARCH not set')
        return 1

    with open(os.path.join(build_dir, 'boot_tests.json')) as file:
        boot_tests = json.load(file)

    with open(os.path.join(build_dir, 'images.json')) as file:
        images = json.load(file)

    def qemu_test(test):
        label = test['qemu_kernel_label']
        for image in images:
            if image.get('label') == label:
                name = image['name']
                if name.startswith('_qemu_phys_test.') and name.endswith(
                        '.executable'):
                    name = name[len('_qemu_phys_test.'):-len('.executable')]
                else:
                    name = test['name']
                return {
                    'label': label,
                    'disabled': test['disabled'],
                    'name': name,
                    'kernel_path': image['path'],
                    'ramdisk_path': test['path'],
                }
        print('%s missing from images.json' % label)
        sys.exit(1)

    all_qemu = [
        qemu_test(test)
        for test in boot_tests
        if test['cpu'] == test_cpu and 'qemu_kernel_label' in test
    ]

    all_zbi = [
        test for test in boot_tests
        if test['cpu'] == test_cpu and 'qemu_kernel_label' not in test
    ]

    if not args.name:
        for kind, tests in {'ZBI': all_zbi, 'QEMU kernel': all_qemu}.items():
            print('\nAvailable %s tests:' % kind)
            for test in tests:
                print(
                    '  %s%s from %s' % (
                        test['name'], ' (disabled)' if test['disabled'] else '',
                        test['label']))
        return 0

    zbis = [
        os.path.join(build_dir, test['path'])
        for test in all_zbi
        if test['name'] == args.name
    ]

    qemus = [
        (
            os.path.join(build_dir, test['kernel_path']),
            os.path.join(build_dir, test['ramdisk_path']))
        for test in all_qemu
        if test['name'] == args.name
    ]

    if not zbis and not qemus:
        print('Cannot find ZBI test %s for %s' % (args.name, test_cpu))
        return 1

    if len(zbis + qemus) > 1:
        print('Multiple matches for %s:' % name)
        for path in zbis + qemus:
            print(path)
        return 1

    if args.boot and qemus:
        print('Cannot use --boot with QEMU-only test %s' % args.name)
        return 1

    if args.boot:
        bootserver = find_bootserver(build_dir)
        cmd = [bootserver, '--boot'] + zbis + args.args
    else:
        cmd = ['fx', 'qemu'] + args.args

        if zbis:
            cmd += ['-z'] + zbis
        else:
            [(kernel, zbi)] = qemus
            cmd += ['-t', kernel, '-z', zbi]

    for arg in args.cmdline:
        cmd += ['-c', arg]

    # Prevents QEMU from boot-looping, as most ZBI tests do not have a means of
    # gracefully shutting down.
    if not args.boot:
        cmd += ['--', '-no-reboot']

    print('+ %s' % ' '.join(map(shlex.quote, cmd)))
    return subprocess.run(cmd).returncode


if __name__ == '__main__':
    sys.exit(main())
