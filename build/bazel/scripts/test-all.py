#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Run misc. tests to ensure Bazel support works as expected!.

By default, this will invoker prepare-fuchsia-checkout.py and do an
`fx clean` operation before anything else. Use --skip-prepare or
--skip-clean to skip these steps, which is useful when adding new
tests.

By default, command outputs is sent to a log file in your TMPDIR, whose
name is printed when this script starts. Use --log-file FILE to send them
to a specific file instead, --verbose to print everything on the current
terminal. Finally --quiet will remove normal outputs (but will not
disable logging, use `--log-file /dev/null` if this is really needed).

This script can be run on CQ, but keep in mind that this requires accessing
the network to download various prebuilts. This happens both during the
prepare-fuchsia-checkout.py step, as well as during the Bazel build
itself, due to the current state of the @rules_fuchsia repository rules
being used (to be fixed in the future, of course).
"""

import argparse
import os
import subprocess
import sys
import tempfile

_SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument(
        '--fuchsia-dir', help='Specify top-level Fuchsia directory.')

    parser.add_argument(
        '--verbose',
        action='store_true',
        help='Print everything to current terminal instead of logging to file.')

    parser.add_argument('--log-file', help='Specify log file.')

    parser.add_argument(
        '--skip-prepare',
        action='store_true',
        help='Skip the inital checkout preparation step.')
    parser.add_argument(
        '--skip-clean',
        action='store_true',
        help='Skip the output directory cleanup step, implies --skip-prepare.')
    parser.add_argument(
        '--quiet', action='store_true', help='Reduce verbosity.')

    args = parser.parse_args()

    if args.skip_clean:
        args.skip_prepare = True

    if args.verbose:
        log_file = None
    elif args.log_file:
        log_file_path = args.log_file
        log_file = open(log_file_path, 'w')
    else:
        log_file = tempfile.NamedTemporaryFile(mode='w', delete=False)
        log_file_path = log_file.name

    if not args.fuchsia_dir:
        # Assumes this is under //build/bazel/scripts/
        args.fuchsia_dir = os.path.join(
            os.path.dirname(__file__), '..', '..', '..')

    fuchsia_dir = os.path.abspath(args.fuchsia_dir)

    def log(message):
        if log_file:
            print('[test-all] ' + message, file=log_file, flush=True)
        if not args.quiet:
            print(message, flush=True)

    if log_file:
        log('Logging enabled, use: tail -f ' + log_file_path)

    def run_command(args):
        subprocess.check_call(
            args,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            cwd=fuchsia_dir,
            text=True)

    def run_fx_command(args):
        run_command(['scripts/fx'] + args)

    def get_command_output(args):
        return subprocess.check_output(
            args, stderr=subprocess.DEVNULL, cwd=fuchsia_dir, text=True)

    def get_fx_command_output(args):
        return get_command_output(['scripts/fx'] + args)

    def check_test_query(name, path):
        """Run a Bazel query and verify its output.

        Args:
           name: Name of query check for the log.
           path: Directory path, this must contain two files named
               'test-query.patterns' and 'test-query.expected_output'.
               The first one contains a list of Bazel query patterns
               (one per line). The second one corresponds to the
               expected output for the query.
        Returns:
            True on success, False on error (after displaying an error
            message that shows the mismatched actual and expected outputs.
        """
        with open(os.path.join(path, 'test-query.patterns')) as f:
            query_patterns = f.read().splitlines()

        with open(os.path.join(path, 'test-query.expected_output')) as f:
            expected_output = f.read()

        output = get_fx_command_output(['bazel', 'query'] + query_patterns)
        if output != expected_output:
            log('ERROR: Unexpectedoutput for %s query:' % name)
            log(
                'ACTUAL [[[\n%s\n]]] EXPECTED [[[\n%s\n]]]' %
                (output, expected_output))
            return False

        return True

    if args.skip_prepare:
        log('Skipping preparation step due to --skip-prepare.')
    else:
        log('Preparing Fuchsia checkout at: ' + fuchsia_dir)
        run_command(
            [
                os.path.join(_SCRIPT_DIR, 'prepare-fuchsia-checkout.py'),
                '--fuchsia-dir', fuchsia_dir
            ])

    if args.skip_clean:
        log('Skipping cleanup step due to --skip-clean.')
    else:
        log('Cleaning current output directory.')
        run_fx_command(['clean'])

    log('Generating bazel workspace and repositories.')
    run_fx_command(['build', ':bazel_workspace'])

    log('bazel_build_action() checks.')
    run_fx_command(['build', 'build/bazel/examples/build_action'])

    log('bazel_inputs_resource_directory() check.')
    if not check_test_query('bazel_input_resource_directory', os.path.join(
            args.fuchsia_dir,
            'build/bazel/examples/bazel_input_resource_directory')):
        return 1

    log('Checking `fx bazel` command.')
    run_fx_command(['bazel', 'run', '//build/bazel/examples/hello_world'])

    log('Done!')
    return 0


if __name__ == '__main__':
    sys.exit(main())
