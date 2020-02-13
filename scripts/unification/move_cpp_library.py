#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import fileinput
import json
import os
import re
import sys

from common import (FUCHSIA_ROOT, run_command, is_tree_clean, fx_format,
                    is_in_fuchsia_project)
from get_library_stats import get_library_stats, Sdk

SCRIPT_LABEL = '//' + os.path.relpath(os.path.abspath(__file__),
                                      start=FUCHSIA_ROOT)


def main():
    parser = argparse.ArgumentParser(
            description='Moves a C/C++ library from //zircon to //sdk')
    parser.add_argument('lib',
                        help='Name of the library folder to migrate, e.g. '
                             'ulib/foo or dev/lib/bar')
    args = parser.parse_args()

    # Check that the fuchsia.git tree is clean.
    if not is_tree_clean():
        return 1

    # Verify that the library may be migrated at this time.
    build_path = os.path.join(FUCHSIA_ROOT, 'zircon', 'system', args.lib,
                              'BUILD.gn')
    base_label = '//zircon/system/' + args.lib
    stats = get_library_stats(build_path)

    # No kernel!
    has_kernel = len([s for s in stats if s.kernel]) != 0
    if has_kernel:
        print('Some libraries in the given folder are used in the kernel and '
              'may not be migrated at the moment')
        return 1

    # Only source libraries!
    non_source_sdk = len([s for s in stats if s.sdk != Sdk.SOURCE]) != 0
    if non_source_sdk:
        print('Can only convert libraries exported as "sources" for now')
        return 1

    # Rewrite the library's build file.
    import_added = False
    for line in fileinput.FileInput(build_path, inplace=True):
        sys.stdout.write(line)
        if not line.strip() and not import_added:
            import_added = True
            sys.stdout.write('##########################################\n')
            sys.stdout.write('# Though under //zircon, this build file #\n')
            sys.stdout.write('# is meant to be used in the Fuchsia GN  #\n')
            sys.stdout.write('# build.                                 #\n')
            sys.stdout.write('# See fxb/36139.                         #\n')
            sys.stdout.write('##########################################\n')
            sys.stdout.write('\n')
            sys.stdout.write('assert(!defined(zx) || zx != "/", "This file can only be used in the Fuchsia GN build.")\n')
            sys.stdout.write('\n')
            sys.stdout.write('import("//build/unification/zx_library.gni")\n')
            sys.stdout.write('\n')
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
                for name in [s.name for s in stats]:
                    new_label = '"' + base_label
                    if os.path.basename(new_label) != name:
                        new_label = new_label + ':' + name
                    new_label = new_label + '"'
                    line, count = re.subn('"//zircon/public/lib/' + name + '"',
                                          new_label, line)
                    if count:
                        has_matches = True
                sys.stdout.write(line)
            if has_matches:
                fx_format(file_path)
                if not is_in_fuchsia_project(file_path):
                    multiple_projects_affected = True

    # Generate an alias for the library under //zircon/public/lib if a soft
    # transition is necessary.
    if multiple_projects_affected:
        alias_path = os.path.join(FUCHSIA_ROOT, 'build', 'unification',
                                  'zircon_library_mappings.json')
        with open(alias_path, 'r') as alias_file:
            data = json.load(alias_file)
        for s in stats:
            data.append({
                'name': s.name,
                'sdk': s.sdk_publishable,
                'label': base_label + ":" + s.name,
            })
        data = sorted(data, key=lambda item: item['name'])
        with open(alias_path, 'w') as alias_file:
            json.dump(data, alias_file, indent=2, sort_keys=True,
                      separators=(',', ': '))

    # Remove the reference in the ZN aggregation target.
    aggregation_path = os.path.join(FUCHSIA_ROOT, 'zircon', 'system',
                                    os.path.dirname(args.lib), 'BUILD.gn')
    folder = os.path.basename(args.lib)
    for line in fileinput.FileInput(aggregation_path, inplace=True):
        for s in stats:
            if (not '"' + folder + ':' + name + '"' in line and
                not '"' + folder + '"' in line):
                sys.stdout.write(line)

    # Create a commit.
    lib = args.lib.replace('/', '_')
    run_command(['git', 'checkout', '-b', 'lib-move-' + lib, 'JIRI_HEAD'])
    run_command(['git', 'add', FUCHSIA_ROOT])
    message = [
        '[unification] Move //zircon/system/' + lib + ' to the GN build',
        '',
        'Generated with: ' + SCRIPT_LABEL + ' ' + args.lib,
        '',
        'Bug: 36548'
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
        print('   targets under //build/unification/zircon_library_mappings.json')
    else:
        print('Change is ready, use "jiri upload" to start the review process.')

    return 0


if __name__ == '__main__':
    sys.exit(main())
