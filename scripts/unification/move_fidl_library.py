#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import fileinput
import os
import re
import shutil
import sys

from common import (FUCHSIA_ROOT, run_command, is_tree_clean,
                    is_in_fuchsia_project, fx_format)


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
    if not is_tree_clean():
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
            dest_file = os.path.join(sdk_dir, file)
            fx_format(dest_file)
        break

    # Edit the build file in its new location.
    in_sdk = False
    build_path = os.path.join(sdk_dir, 'BUILD.gn')
    for line in fileinput.FileInput(build_path, inplace=True):
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
    fx_format(build_path)

    # Track whether fuchsia.git was the only affected project.
    multiple_projects_affected = False

    # Edit references to the library.
    for base, _, files in os.walk(FUCHSIA_ROOT):
        for file in files:
            if file != 'BUILD.gn':
                continue
            has_matches = False
            file_path = os.path.join(base, file)
            for line in fileinput.FileInput(file_path, inplace=True):
                match = re.search('"//zircon/system/fidl/' + lib_with_dash + '(?::(?P<target>[^"]+))?"',
                                  line)
                if match:
                    has_matches = True
                    original_target = match.group('target')
                    if original_target:
                        if original_target == 'c':
                            target = lib + '_c'
                        elif original_target == 'c.headers':
                            target = lib + '_c'
                        elif original_target == 'llcpp':
                            target = lib + '_llcpp'
                        elif original_target == 'llcpp.headers':
                            target = lib + '_llcpp'
                        else:
                            target = original_target.replace(lib_with_dash, lib)
                        line = line.replace('"//zircon/system/fidl/' + lib_with_dash + ':' + original_target + '"',
                                            '"//sdk/fidl/' + lib + ':' + target + '"')
                    else:
                        line = line.replace('"//zircon/system/fidl/' + lib_with_dash + '"',
                                            '"//sdk/fidl/' + lib + '"')
                sys.stdout.write(line)
            if has_matches:
                # Format the file.
                fx_format(file_path)
                if not is_in_fuchsia_project(file_path):
                    multiple_projects_affected = True

    if multiple_projects_affected:
        # Set up an alias in the old location.
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

    if multiple_projects_affected:
        print('*** Warning: multiple Git projects were affected by this move!')
        print('Run jiri status to view affected projects.')
        print('Staging procedure:')
        print(' - use "jiri upload" to start the review process on the fuchsia.git change;')
        print(' - prepare dependent CLs for each affected project and get them review;')
        print(' - when the fuchsia.git change has rolled into GI, get the other CLs submitted;')
        print(' - when these CLs have rolled into GI, prepare a change to remove the forwarding')
        print('   target under //zircon/system/fidl/' + lib_with_dash)
    else:
        print('Change is ready, use "jiri upload" to start the review process.')

    return 0


if __name__ == '__main__':
    sys.exit(main())
