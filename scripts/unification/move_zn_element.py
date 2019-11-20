#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import fileinput
import os
import re
import subprocess
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPTS_DIR = os.path.dirname(SCRIPT_DIR)
FUCHSIA_ROOT = os.path.dirname(SCRIPTS_DIR)
FX = os.path.join(SCRIPTS_DIR, 'fx')

BINARY_TYPES = {
    'zx_driver': 'driver_module',
    'zx_executable': 'executable',
    'zx_test': 'test',
    'zx_test_driver': 'driver_module',
}


def run_command(command):
    return subprocess.check_output(command, cwd=FUCHSIA_ROOT)


def locate_build_files(base):
    result = []
    for root, dirs, files in os.walk(os.path.join(FUCHSIA_ROOT, 'zircon',
                                    'system', base)):
        for file in files:
            if file == 'BUILD.gn':
                result.append(os.path.join(root, file))
    return result


def transform_build_file(build):
    # First pass: identify contents of the build file.
    binaries = []
    has_test_binaries = False
    binary_types = BINARY_TYPES.keys()
    unclear_types = ['library']
    n_lines = 0
    with open(build, 'r') as build_file:
        lines = build_file.readlines()
        n_lines = len(lines)
        for line in lines:
            match = re.match(r'\A([^\(]+)\("([^"]+)"\)', line)
            if match:
                type, name = match.groups()
                if type in binary_types:
                    binaries.append(name)
                    if type == 'zx_test':
                        has_test_binaries = True
                if type in unclear_types:
                    print('Warning: target ' + name + ' of type ' + type + ' '
                          'needs to be manually converted.')

    # Second pass: rewrite contents to match GN build standards.
    imports_added = False
    for line in fileinput.FileInput(build, inplace=True):
        # Apply required edits.
        # Update target types.
        starting_type = ''
        for type in binary_types:
            new_type_line = line.replace(type, BINARY_TYPES[type])
            if new_type_line != line:
                starting_type = type
                line = new_type_line
                break
        # Remove references to libzircon.
        if '$zx/system/ulib/zircon' in line:
            line = ''
        # Update references to libraries.
        line = line.replace('$zx/system/ulib', '//zircon/public/lib')
        # Update references to Zircon in general.
        line = line.replace('$zx', '//zircon')
        # Print the line, if any content is left.
        if line:
            sys.stdout.write(line)

        # Insert required imports at the start of the file.
        if not line.strip() and not imports_added:
            imports_added = True
            sys.stdout.write('##########################################\n')
            sys.stdout.write('# Though under //zircon, this build file #\n')
            sys.stdout.write('# is meant to be used in the Fuchsia GN  #\n')
            sys.stdout.write('# build.                                 #\n')
            sys.stdout.write('# See fxb/36139.                         #\n')
            sys.stdout.write('##########################################\n')
            sys.stdout.write('\n')
            sys.stdout.write('assert(!defined(zx) || zx != "/", "This file can only be used in the Fuchsia GN build.")\n')
            sys.stdout.write('\n')
            if has_test_binaries:
                sys.stdout.write('import("//build/test.gni")\n')
            sys.stdout.write('import("//build/unification/images/migrated_manifest.gni")\n')
            sys.stdout.write('\n')

        # Add extra parameters to tests.
        if starting_type == 'zx_test':
            sys.stdout.write('  # Dependent manifests unfortunately cannot be marked as `testonly`.\n')
            sys.stdout.write('  # Remove when converting this file to proper GN build idioms.\n')
            sys.stdout.write('  testonly = false\n')

        if starting_type == 'zx_test_driver':
            sys.stdout.write('  test = true\n')

        if starting_type in ['zx_driver', 'zx_test_driver']:
            sys.stdout.write('  defines = [ "_ALL_SOURCE" ]\n')
            sys.stdout.write('  configs += [ "//build/config/fuchsia:enable_zircon_asserts" ]\n')
            sys.stdout.write('  configs -= [ "//build/config/fuchsia:no_cpp_standard_library" ]\n')
            sys.stdout.write('  configs += [ "//build/config/fuchsia:static_cpp_standard_library" ]\n')


    # Third pass: add manifest targets at the end of the file.
    with open(build, 'a') as build_file:
        for binary in binaries:
            build_file.write('\n')
            build_file.write('migrated_manifest("' + binary + '-manifest") {\n')
            build_file.write('  deps = [\n')
            build_file.write('    ":' + binary + '",\n')
            build_file.write('  ]\n')
            build_file.write('}\n')

    # Format the file.
    run_command([FX, 'format-code', '--files=' + build])

    return 0


def main():
    parser = argparse.ArgumentParser(
            description='Moves a binary from ZN to GN.')
    parser.add_argument('binary',
                        help='The binary under //zircon/system to migrate, '
                             'e.g. uapp/audio, utest/fit, dev/bus/pci')
    args = parser.parse_args()

    # Check that the fuchsia.git tree is clean.
    diff = run_command(['git', 'status', '--porcelain'])
    if diff:
        print('Please make sure your tree is clean before running this script')
        print(diff)
        return 1

    # Identify the affected build files.
    build_files = locate_build_files(args.binary)
    if not build_files:
        print('Error: could not find any files for ' + args.binary)
        return 1

    # Confirm with the user that these are the files they want to convert.
    print('The following build file(s) will be converted:')
    for file in build_files:
        print(' - ' + os.path.relpath(file, FUCHSIA_ROOT))
    go_ahead = raw_input('Proceed? (Y/n) ')
    if go_ahead != 'Y':
        print('Aborting')
        return 0

    # Convert the build files.
    for file in build_files:
        transform_build_file(file)

    # Create a commit.
    id = args.binary.replace('/', '_')
    run_command(['git', 'checkout', '-b', 'gn-move-' + id, 'JIRI_HEAD'])
    run_command(['git', 'add', '.'])
    message = [
        '[unification] Move //zircon/system/' + args.binary + ' to the GN build',
        '',
        'Generated with: //scripts/unification/move_zn_binary.py',
        '',
        'Bug: 36139'
    ]
    commit_command = ['git', 'commit', '-a']
    for line in message:
        commit_command += ['-m', line]
    run_command(commit_command)

    print('Base change is ready. Please attempt to build a full system to '
          'identify further required changes.')

    return 0


if __name__ == '__main__':
    sys.exit(main())
