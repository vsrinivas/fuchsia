#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import fileinput
import os
import re
import shutil
import subprocess
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    SCRIPT_DIR))                 # unification


def run_command(command):
    return subprocess.check_output(command, cwd=FUCHSIA_ROOT)


def main():
    parser = argparse.ArgumentParser(
            description='Moves a Banjo library from //zircon to //sdk')
    parser.add_argument('lib',
                        help='Name of the library to migrate')
    args = parser.parse_args()
    lib = args.lib

    # Check that the fuchsia.git tree is clean.
    diff = run_command(['git', 'status', '--porcelain'])
    if diff:
        print('Please make sure your tree is clean before running this script')
        print(diff)
        return 1

    sdk_base = os.path.join(FUCHSIA_ROOT, 'sdk', 'banjo')
    sdk_dir = os.path.join(sdk_base, lib)
    banjo_base = os.path.join(FUCHSIA_ROOT, 'zircon', 'system', 'banjo')
    source_dir = os.path.join(banjo_base, lib)

    # Move the sources.
    shutil.move(source_dir, sdk_base)

    # Edit the build file.
    is_dummy = not (lib.startswith('ddk.protocol') or lib.startswith('ddk.hw'))
    for line in fileinput.FileInput(os.path.join(sdk_dir, 'BUILD.gn'),
                                    inplace=True):
        line = line.replace('$zx_build/public/gn/banjo.gni',
                            '//build/banjo/banjo.gni')
        line = line.replace('banjo_library',
                            'banjo_dummy' if is_dummy else 'banjo')
        line = line.replace('public_deps',
                            'deps')
        line = line.replace('$zx/system/banjo',
                            '//zircon/system/banjo')
        sys.stdout.write(line)

    # Edit references to the library.
    for base, _, files in os.walk(FUCHSIA_ROOT):
        for file in files:
            if file != 'BUILD.gn':
                continue
            for line in fileinput.FileInput(os.path.join(base, file),
                                            inplace=True):
                # Make sure that only exact matches are replaced, as some
                # library names are prefix of other names.
                # Examples:
                # //zircon/s/b/ddk.foo.bar" --> "//sdk/b/foo.bar"
                # //zircon/s/b/ddk.foo.bar:boop" --> "//sdk/b/foo.bar:boop"
                line = re.sub('"(//zircon/system/banjo/' + lib + ')([:"])',
                              '"//sdk/banjo/' + lib + '\\2',
                              line)
                sys.stdout.write(line)
    for line in fileinput.FileInput(os.path.join(banjo_base, 'BUILD.gn'),
                                    inplace=True):
        if not '"' + lib + '"' in line:
            sys.stdout.write(line)

    # Create a commit.
    run_command(['git', 'checkout', '-b', 'banjo-move-' + lib, 'JIRI_HEAD'])
    run_command(['git', 'add', sdk_dir])
    message = [
        '[unification] Move ' + lib + ' to //sdk/banjo',
        '',
        'Generated with: //scripts/unification/move_banjo_library.py ' + lib,
        '',
        'Bug: 36540'
    ]
    commit_command = ['git', 'commit', '-a']
    for line in message:
        commit_command += ['-m', line]
    run_command(commit_command)

    print('Change is ready, use "jiri upload" to start the review process.')

    return 0


if __name__ == '__main__':
    sys.exit(main())
