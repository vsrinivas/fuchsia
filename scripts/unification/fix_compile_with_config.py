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
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(  # scripts
        SCRIPT_DIR))  # unification
if sys.platform.startswith('linux'):
    PLATFORM = 'linux-x64'
elif sys.platform.startswith('darwin'):
    PLATFORM = 'mac-x64'
GN = os.path.join(FUCHSIA_ROOT, 'prebuilt', 'third_party', 'gn', PLATFORM, 'gn')
FX = os.path.join(os.path.dirname(SCRIPT_DIR), 'fx')
GIT_BRANCH_FORBIDDEN_CHARS = '.~^:/\\'


def run_command(command):
    return subprocess.check_output(
        command, stderr=subprocess.STDOUT, cwd=FUCHSIA_ROOT)


def main():
    parser = argparse.ArgumentParser(
        description='Adds a given config to all compile targets with '
        'a given compiler error')
    parser.add_argument('--error', help='Compiler error marker')
    parser.add_argument('--config', help='Config to add')
    parser.add_argument(
        '--zircon', help='//zircon build (ZN)', action='store_true')
    args = parser.parse_args()
    error = args.error
    config = args.config
    zircon = args.zircon

    # Harvest all compilation errors
    print 'Building...'
    try:
        # On error continue building to discover all failures
        run_command([FX, 'build', '-k0'])
        print 'Build successful!'
        return 0
    except subprocess.CalledProcessError as e:
        build_out = e.output
    error_regex = re.compile('(?:../)*([^:]*):\d*:\d*: error: .*' + error)
    error_files = set()
    for line in build_out.split('\n'):
        match = error_regex.match(line)
        if match:
            path = os.path.normpath(match.group(1))
            if zircon:
              path = '//' + os.path.relpath(path, 'zircon')
            error_files.add(path)
    print 'Sources with compilation errors:'
    print '\n'.join(sorted(error_files))
    print

    # Collect all BUILD.gn files with failing targets
    print 'Resolving failing targets...'
    # TODO support for zircon build
    # --root=zircon refs out/default.zircon <target>
    # --all-toolchains
    # //:instrumented-ulib-redirect.asan(//public/gn/toolchain:user-arm64-clang)
    # //:instrumented-ulib-redirect.asan(//public/gn/toolchain:user-x64-clang)
    outdir = 'out/default.zircon' if zircon else 'out/default'
    refs_command = [GN, 'refs']
    if zircon:
        refs_command += [
            'out/default.zircon', '--root=zircon', '--all-toolchains',
            '//:instrumented-ulib-redirect.asan(//public/gn/toolchain:user-arm64-clang)',
            '//:instrumented-ulib-redirect.asan(//public/gn/toolchain:user-x64-clang)'
        ]
    else:
        refs_command += ['out/default']
    refs_command += sorted(error_files)
    refs_out = run_command(refs_command)
    error_targets = set(refs_out.strip().split('\n'))
    print 'Failing BUILD.gn files:'
    print '\n'.join(sorted(error_targets))
    print

    # Fix failing targets
    ref_regex = re.compile('//([^:]*):([^.(]*).*')
    for target in error_targets:
        match = ref_regex.match(target)
        build_dir, target_name = match.groups()
        target_regex = re.compile('\w*\("' + target_name + '"\) {')
        build_file = os.path.join(build_dir, 'BUILD.gn')
        # Format file before processing
        run_command([FX, 'format-code', '--files=' + build_file])
        print 'Fixing', build_file
        # Below is a duct-tape approach to editing GN files that is not
        # robust to all possible inputs.
        # Consider writing ANTLR grammar for GN and then generating
        # Python lexer and parser code to properly process these files.
        in_target = False
        config_printed = False
        in_public_configs = False
        curly_brace_depth = 0
        if zircon:
            build_file = os.path.join('zircon', build_file)
        build_file = os.path.join(FUCHSIA_ROOT, build_file)
        for line in fileinput.FileInput(build_file, inplace=True):
            # TODO: make this robust to escaping
            curly_brace_depth += line.count('{') - line.count('}')
            assert curly_brace_depth >= 0
            if config_printed:
                # We already printed the config, keep running until we exit the target
                if curly_brace_depth == target_end_depth:
                    in_target = False
            elif not in_target:
                # Try to enter target
                in_target = bool(target_regex.findall(line))
                if in_target:
                    target_end_depth = curly_brace_depth - 1
            elif curly_brace_depth > target_end_depth + 1:
                # Ignore inner blocks such as inner definitions and conditionals
                pass
            elif curly_brace_depth == target_end_depth:
                # Last chance to print config before exiting
                print 'public_configs = [ "' + config + '" ]'
                config_printed = True
                in_target = False
            elif 'public_configs = [ "' in line and config in line:
                config_printed = True
            elif 'public_configs = [ "' in line:
                line = line[:-3] + ', "' + config + '" ]\n'
                config_printed = True
            elif 'public_configs = [' in line:
                in_public_configs = True
            elif in_public_configs and line.strip() == '"' + config + '",':
                sys.stderr.write('Case 1 ' + line)
                in_public_configs = False
                config_printed = True
            elif in_public_configs and line.strip() == ']':
                sys.stderr.write('Case 2 ' + line)
                print '"' + config + '",'
                in_public_configs = False
                config_printed = True
            print line,
            if config_printed and not in_target:
                # Reset for a possible redefinition of the same target
                # (e.g. within another conditional block)
                config_printed = False
        run_command([FX, 'format-code', '--files=' + build_file])
        run_command(['git', 'add', build_file])

    # Create a commit.
    run_command(
        [
            'git', 'checkout', '-b',
            'config-' + config.translate(None, GIT_BRANCH_FORBIDDEN_CHARS),
            'JIRI_HEAD'
        ])
    message = [
        '[config] configs += "' + config + '"', '',
        'Generated with: //scripts/unification/fix_compile_with_config.py ' +
        '--error="' + error + '" --config="' + config + '"', ''
    ]
    commit_command = ['git', 'commit', '-a']
    for line in message:
        commit_command += ['-m', line]
    run_command(commit_command)

    print 'Change is ready, use "jiri upload" to start the review process.'

    return 0


if __name__ == '__main__':
    sys.exit(main())
