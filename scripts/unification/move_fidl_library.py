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
            description='Moves a FIDL library from //zircon to //sdk')
    parser.add_argument('lib',
                        help='Name of the library to migrate')
    args = parser.parse_args()

    # Accept library names with dots or dashes.
    lib = args.lib.replace('-', '.')
    lib_with_dash = args.lib.replace('.', '-')

    # Check that the fuchsia.git tree is clean.
    diff = run_command(['git', 'status', '--porcelain'])
    if diff:
        print('Please make sure your tree is clean before running this script')
        print(diff)
        return 1

    sdk_base = os.path.join(FUCHSIA_ROOT, 'sdk', 'fidl')
    sdk_dir = os.path.join(sdk_base, lib)
    fidl_base = os.path.join(FUCHSIA_ROOT, 'zircon', 'system', 'fidl')
    source_dir = os.path.join(fidl_base, lib_with_dash)

    # Move the sources.
    # The destination directory sometimes already exists.
    if not os.path.isdir(sdk_dir):
        os.mkdir(sdk_dir)
    for _, _, files in os.walk(source_dir):
        for file in files:
            shutil.move(os.path.join(source_dir, file), sdk_dir)
        break

    # Edit the build file in its new location.
    in_sdk = False
    for line in fileinput.FileInput(os.path.join(sdk_dir, 'BUILD.gn'),
                                    inplace=True):
        if 'sdk = false' in line:
            continue
        if 'sdk = true' in line:
            in_sdk = True
            print('  sdk_category = "partner"')
            print('  api = "' + lib + '.api"')
            continue
        line = line.replace('$zx_build/public/gn/fidl.gni',
                            '//build/fidl/fidl.gni')
        line = line.replace('fidl_library',
                            'fidl')
        line = line.replace(lib_with_dash,
                            lib)
        line = line.replace('$zx/system/fidl',
                            '//zircon/system/fidl')
        sys.stdout.write(line)

    # Set up an alias in the old location.
    # Fixing references to the library will likely require a soft transition.
    with open(os.path.join(source_dir, 'BUILD.gn'), 'w') as build_file:
        build_file.writelines([
            '# Copyright 2020 The Fuchsia Authors. All rights reserved.\n',
            '# Use of this source code is governed by a BSD-style license that can be\n',
            '# found in the LICENSE file.\n',
            '\n',
            'import("//build/unification/fidl_alias.gni")\n',
            '\n',
            'fidl_alias("%s") {\n' % lib_with_dash,
            '  sdk_category = "partner"\n' if in_sdk else '\n',
            '}\n',
        ])

    # Edit references to the library.
    # Only editing the ZN file listing all FIDL libraries.
    for line in fileinput.FileInput(os.path.join(fidl_base, 'BUILD.gn'),
                                    inplace=True):
        if not '"' + lib_with_dash + '"' in line:
            sys.stdout.write(line)

    # Create a commit.
    run_command(['git', 'checkout', '-b', 'fidl-move-' + lib, 'JIRI_HEAD'])
    run_command(['git', 'add', sdk_dir])
    message = [
        '[unification] Move ' + lib + ' to //sdk/fidl',
        '',
        'Generated with: //scripts/unification/move_fidl_library.py ' + lib,
        '',
        'Bug: 36547'
    ]
    commit_command = ['git', 'commit', '-a']
    for line in message:
        commit_command += ['-m', line]
    run_command(commit_command)

    print('Change is ready, use "jiri upload" to start the review process.')

    return 0


if __name__ == '__main__':
    sys.exit(main())
