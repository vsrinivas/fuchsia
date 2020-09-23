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
import tempfile


from common import (FUCHSIA_ROOT, run_command, is_tree_clean, fx_format,
                    is_in_fuchsia_project)
from get_library_stats import get_library_stats, Sdk

SCRIPT_LABEL = '//' + os.path.relpath(os.path.abspath(__file__),
                                      start=FUCHSIA_ROOT)


class Library(object):

    def __init__(self, name):
        self.name = name
        if name.startswith('third_party'):
          self.base_label = '//zircon/' + name
          self.build_path = os.path.join(FUCHSIA_ROOT, 'zircon', name,
                                         'BUILD.gn')
        else:
          self.base_label = '//zircon/system/' + name
          self.build_path = os.path.join(FUCHSIA_ROOT, 'zircon', 'system', name,
                                         'BUILD.gn')
        self.stats = get_library_stats(self.build_path)
        self.build_files = []
        self.cross_project = False
        self.has_kernel = any(s.kernel for s in self.stats)
        self.has_unexported = any(s.sdk == Sdk.NOPE for s in self.stats)
        self.has_sdk = any(s.sdk_publishable for s in self.stats)
        self.has_shared = any(s.sdk == Sdk.SHARED for s in self.stats)


def replace_lines(path, replacer):
    '''Replaces lines in a file based on the output of the given function.'''
    for line in fileinput.FileInput(path, inplace=True):
        replacement = replacer(line)
        if replacement is None:
            # No modification needed.
            sys.stdout.write(line)
            continue
        if not replacement:
            # The line should be removed.
            continue
        # The line should be updated.
        sys.stdout.write(replacement)


def main():
    parser = argparse.ArgumentParser(
            description='Moves a C/C++ library from //zircon to //sdk')
    parser.add_argument('--lib',
                        help='Name of the library folder to migrate, e.g. '
                             'ulib/foo or dev/lib/bar',
                        action='append')
    args = parser.parse_args()

    if not args.lib:
        print('Need to specify at least one library.')
        return 1

    # Check that the fuchsia.git tree is clean.
    if not is_tree_clean():
        return 1

    movable_libs = []
    for lib in args.lib:
        # Verify that the library may be migrated at this time.
        library = Library(lib)
        if not os.path.exists(library.build_path):
            print('Could not locate library ' + lib + ', ignoring')
            continue

        # No kernel!
        if library.has_kernel:
            print('Some libraries are used in the kernel and may not be '
                  'migrated at the moment, ignoring ' + lib)
            continue

        # Only libraries that have been exported to the GN build already!
        if library.has_unexported:
            print('Can only convert libraries already exported to the GN build,'
                  ' ignoring ' + lib)
            continue

        # Gather build files with references to the library.
        for base, _, files in os.walk(FUCHSIA_ROOT):
            for file in files:
                if file != 'BUILD.gn':
                    continue
                file_path = os.path.join(base, file)
                with open(file_path, 'r') as build_file:
                    content = build_file.read()
                    for name in [s.name for s in library.stats]:
                        reference = '"//zircon/public/lib/' + name + '"'
                        if reference in content:
                            library.build_files.append(file_path)
                            if not is_in_fuchsia_project(file_path):
                                library.cross_project = True
                            break

        movable_libs.append(library)

    if not movable_libs:
        print('Could not find any library to convert, aborting')
        return 1

    solo_libs = [l.name for l in movable_libs
                 if l.cross_project or l.has_shared]
    if solo_libs and len(movable_libs) > 1:
        print('These libraries may only be moved in a dedicated change: ' +
              ', '.join(solo_libs))
        return 1

    for library in movable_libs:
        # Rewrite the library's build file.
        import_added = False
        for line in fileinput.FileInput(library.build_path, inplace=True):
            # Remove references to libzircon.
            if '$zx/system/ulib/zircon' in line and not 'zircon-internal' in line:
                line = ''
            # Update references to libraries.
            line = line.replace('$zx/system/ulib', '//zircon/public/lib')
            line = line.replace('$zx/system/dev/lib', '//zircon/public/lib')
            # Update known configs.
            line = line.replace('$zx_build/public/gn/config:static-libc++',
                                '//build/config/fuchsia:static_cpp_standard_library')
            # Update references to Zircon in general.
            line = line.replace('$zx', '//zircon')
            # Update deps on libdriver.
            line = line.replace('//zircon/public/lib/driver',
                                '//src/devices/lib/driver')
            # Remove header target specifier.
            line = line.replace('.headers', '')
            line = line.replace(':headers', '')
            sys.stdout.write(line)

            if not line.strip() and not import_added:
                import_added = True
                sys.stdout.write('##########################################\n')
                sys.stdout.write('# Though under //zircon, this build file #\n')
                sys.stdout.write('# is meant to be used in the Fuchsia GN  #\n')
                sys.stdout.write('# build.                                 #\n')
                sys.stdout.write('# See fxbug.dev/36548.                   #\n')
                sys.stdout.write('##########################################\n')
                sys.stdout.write('\n')
                sys.stdout.write('assert(!defined(zx) || zx != "/", "This file can only be used in the Fuchsia GN build.")\n')
                sys.stdout.write('\n')
                sys.stdout.write('import("//build/unification/zx_library.gni")\n')
                sys.stdout.write('\n')
        fx_format(library.build_path)

        # Edit references to the library.
        for file_path in library.build_files:
            for line in fileinput.FileInput(file_path, inplace=True):
                for name in [s.name for s in library.stats]:
                    new_label = '"' + library.base_label
                    if os.path.basename(new_label) != name:
                        new_label = new_label + ':' + name
                    new_label = new_label + '"'
                    line = re.sub('"//zircon/public/lib/' + name + '"',
                                  new_label, line)
                sys.stdout.write(line)
            fx_format(file_path)

        # Remove references to the library in the unification scaffolding if
        # necessary.
        if library.has_shared:
            unification_path = os.path.join(FUCHSIA_ROOT, 'build',
                                            'unification', 'images', 'BUILD.gn')
            def unification_replacer(line):
                for name in [s.name for s in library.stats]:
                    if re.match('^\s*"' + name + '",$', line):
                        # Remove the line.
                        return ''
            replace_lines(unification_path, unification_replacer)

        # Generate an alias for the library under //zircon/public/lib if a soft
        # transition is necessary.
        if library.cross_project:
            alias_path = os.path.join(FUCHSIA_ROOT, 'build', 'unification',
                                      'zircon_library_mappings.json')
            with open(alias_path, 'r') as alias_file:
                data = json.load(alias_file)
            for s in library.stats:
                data.append({
                    'name': s.name,
                    'sdk': s.sdk_publishable,
                    'label': library.base_label + ":" + s.name,
                })
            data = sorted(data, key=lambda item: item['name'])
            with open(alias_path, 'w') as alias_file:
                json.dump(data, alias_file, indent=2, sort_keys=True,
                          separators=(',', ': '))

        # Update references to the library if it belongs to any SDK.
        if library.has_sdk:
            sdk_path = os.path.join(FUCHSIA_ROOT, 'sdk', 'BUILD.gn')
            folder = os.path.basename(library.name)
            def sdk_replacer(line):
                for name in [s.name for s in library.stats]:
                    if '"//zircon/public/lib/' + folder + ':' + name + '_sdk' + '"' in line:
                        return line.replace('public/lib', 'system/ulib')
            replace_lines(sdk_path, sdk_replacer)
            fx_format(sdk_path)

        # Remove the reference in the ZN aggregation target.
        aggregation_path = os.path.join(FUCHSIA_ROOT, 'zircon', 'system',
                                        os.path.dirname(library.name),
                                        'BUILD.gn')
        if os.path.exists(aggregation_path):
            folder = os.path.basename(library.name)
            def aggregation_replacer(line):
                for name in [s.name for s in library.stats]:
                    if ('"' + folder + ':' + name + '"' in line or
                        '"' + folder + '"' in line):
                        return ''
            replace_lines(aggregation_path, aggregation_replacer)
        else:
            print('Warning: some references to ' + lib + ' might still exist '
                  'in the ZN build, please remove them manually')

    # Create a commit.
    libs = sorted([l.name for l in movable_libs])
    under_libs = [l.replace('/', '_') for l in libs]
    branch_name = 'lib-move-' + under_libs[0]
    lib_name = '//zircon/system/' + libs[0]
    if len(libs) > 1:
        branch_name += '-and-co'
        lib_name += ' and others'
    run_command(['git', 'checkout', '-b', branch_name, 'JIRI_HEAD'])
    run_command(['git', 'add', FUCHSIA_ROOT])
    message = [
        '[unification] Move ' + lib_name + ' to the GN build',
        '',
        'Affected libraries:'
    ] + ['//zircon/system/' + l for l in libs]
    if any(l.has_shared for l in movable_libs):
        message += [
            '',
            'scripts/unification/verify_element_move.py --reference local/initial.json:',
            '',
            'TODO PASTE VERIFICATION RESULT HERE',
        ]
    message += [
        '',
        'Generated with ' + SCRIPT_LABEL,
        '',
        'Bug: 36548'
    ]
    fd, message_path = tempfile.mkstemp()
    with open(message_path, 'w') as message_file:
        message_file.write('\n'.join(message))
    commit_command = ['git', 'commit', '-a', '-F', message_path]
    run_command(commit_command)
    os.close(fd)
    os.remove(message_path)

    if any(l.cross_project for l in movable_libs):
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
