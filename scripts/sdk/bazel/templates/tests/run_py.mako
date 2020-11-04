#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
from subprocess import check_output, Popen
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


ARCHES = [
% for arch in data.arches:
    '${arch.short_name}',
% endfor
]


def find_bazel(opt_path):
    if opt_path and os.path.isfile(opt_path) and os.access(opt_path, os.X_OK):
        return (os.path.realpath(opt_path), True)
    for dir in os.environ["PATH"].split(os.pathsep):
        path = os.path.join(dir, 'bazel')
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return (os.path.realpath(path), True)
    return ('bazel', False)


class BazelTester(object):

    def __init__(self, without_sdk, with_ignored, with_output_user_root,
                 bazel_bin, optional_flags=[]):
        self.without_sdk = without_sdk
        self.with_ignored = with_ignored
        self.with_output_user_root = with_output_user_root
        self.bazel_bin = bazel_bin
        self.optional_flags = optional_flags


    def _add_bazel_command_flags(self, command):
        # The following flag is needed because some Dart build rules use a
        # `cfg = "data"` construct that's now an error.
        # TODO: remove this flag when we don't build Dart stuff in this SDK.
        command += ['--incompatible_disallow_data_transition=false']


    def _add_bazel_startup_options(self, command):
        if self.with_output_user_root is not None:
            command += ['--output_user_root=%s' % self.with_output_user_root]


    def _invoke_bazel(self, command, targets):
        invocation = [self.bazel_bin]
        self._add_bazel_startup_options(invocation)
        invocation += [command, '--keep_going']
        self._add_bazel_command_flags(invocation)
        invocation += self.optional_flags
        invocation += targets
        job = Popen(invocation, cwd=SCRIPT_DIR)
        job.communicate()
        return job.returncode


    def _build(self, targets):
        return self._invoke_bazel('build', targets)


    def _test(self, targets):
        return self._invoke_bazel('test', targets)


    def _query(self, query):
        invocation = [self.bazel_bin]
        self._add_bazel_startup_options(invocation)
        invocation += ['query', query]
        self._add_bazel_command_flags(invocation)
        return set(check_output(invocation, cwd=SCRIPT_DIR).decode().splitlines())


    def run(self):
        if not self.without_sdk:
            # Build the SDK contents.
            print('Building SDK contents')
            if self._build(['@fuchsia_sdk//...']):
                return False

        targets = ['//...']
        if not self.with_ignored:
            # Identify and remove ignored targets.
            all_targets = self._query('//...')
            ignored_targets = self._query('attr("tags", "ignored", //...)')
            if ignored_targets:
                # Targets which depend on an ignored target should be ignored too.
                all_ignored_targets = set()
                for target in ignored_targets:
                    all_ignored_targets.add(target)
                    dep_query = 'rdeps("//...", "{}")'.format(target)
                    dependent_targets = self._query(dep_query)
                    all_ignored_targets.update(dependent_targets)
                print('Ignored targets:')
                for target in sorted(all_ignored_targets):
                    print(' - ' + target)
                targets = list(all_targets - all_ignored_targets)

        # Build the tests targets.
        print('Building targets')
        if self._build(targets):
            return False

        # Run tests.
        args = ('attr("tags", "^((?!compile-only).)*$",' +
                ' kind(".*test rule", //...))')
        test_targets = list(self._query(args))
        if not test_targets:
            print('No test to run, done')
            return True
        print('Running test targets')
        return self._test(test_targets) == 0


def main():
    parser = argparse.ArgumentParser(
        description='Runs the SDK tests')
    parser.add_argument('--no-sdk',
                        help='If set, SDK targets are not built.',
                        action='store_true')
    parser.add_argument('--ignored',
                        help='If set, ignored tests are run too.',
                        action='store_true')
    parser.add_argument('--bazel',
                        help='Path to the Bazel tool; otherwise found in PATH')
    parser.add_argument('--once',
                        help='Whether to only run tests once',
                        action='store_true')
    parser.add_argument('--output_user_root',
                        help='If set, passthrough to Bazel to override user root.')
    args = parser.parse_args()

    (bazel, found) = find_bazel(args.bazel)
    if not found:
        print('"%s": command not found' % (bazel))
        return 1

    def print_test_start(arch, cpp_version):
        print('')
        print('-----------------------------------')
        print('| Testing %s / %s' % (arch, cpp_version))
        print('-----------------------------------')

    for arch in ARCHES:
        print_test_start(arch, 'C++14')
        config_flags = ['--config=fuchsia_%s' % arch]
        cpp14_flags = ['--cxxopt=-Wc++14-compat', '--cxxopt=-Wc++17-extensions']
        if not BazelTester(args.no_sdk, args.ignored, args.output_user_root,
                           bazel, optional_flags=config_flags + cpp14_flags).run():
            return 1

        if args.once:
            print('Single iteration requested, done.')
            break

        print_test_start(arch, 'C++17')
        cpp17_flags = ['--cxxopt=-std=c++17', '--cxxopt=-Wc++17-compat']
        if not BazelTester(args.no_sdk, args.ignored, args.output_user_root,
                           bazel, optional_flags=config_flags + cpp17_flags).run():
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
