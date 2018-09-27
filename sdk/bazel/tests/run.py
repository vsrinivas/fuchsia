#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
from subprocess import check_output, Popen
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def invoke_bazel(bazel, command, targets):
    command = [bazel, command, '--config=fuchsia', '--keep_going'] + targets
    job = Popen(command, cwd=SCRIPT_DIR)
    job.communicate()
    return job.returncode


def build(bazel, targets):
    return invoke_bazel(bazel, 'build', targets)


def test(bazel, targets):
    return invoke_bazel(bazel, 'test', targets)


def query(bazel, query):
    command = [bazel, 'query', query]
    return set(check_output(command, cwd=SCRIPT_DIR).splitlines())


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
                        help='Path to the Bazel tool',
                        default='bazel')
    args = parser.parse_args()

    bazel = args.bazel

    if not args.no_sdk:
        # Build the SDK contents.
        print('Building SDK contents')
        if build(bazel, ['@fuchsia_sdk//...']):
            return 1

    targets = ['//...']
    if not args.ignored:
        # Identify and remove ignored targets.
        all_targets = query(bazel, '//...')
        ignored_targets = query(bazel, 'attr("tags", "ignored", //...)')
        if ignored_targets:
            # Targets which depend on an ignored target should be ignored too.
            all_ignored_targets = set()
            for target in ignored_targets:
                all_ignored_targets.add(target)
                dependent_targets = query(bazel,
                                          'rdeps("//...", "{}")'.format(target))
                all_ignored_targets.update(dependent_targets)
            print('Ignored targets:')
            for target in sorted(all_ignored_targets):
                print(' - ' + target)
            targets = list(all_targets - all_ignored_targets)

    # Build the tests targets.
    print('Building test targets')
    if build(bazel, targets):
        return 1

    # Run tests
    args = 'attr("tags", "^((?!compile-only).)*$", kind(".*test rule", //...))'
    test_targets = list(query(bazel, args))
    print('Running test targets')
    return test(bazel, test_targets)


if __name__ == '__main__':
    sys.exit(main())
