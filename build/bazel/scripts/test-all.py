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
import shlex
import subprocess
import sys
import tempfile

_SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))


def get_fx_build_dir(fuchsia_dir):
    """Return the path to the Ninja build directory."""
    fx_build_dir_path = os.path.join(fuchsia_dir, '.fx-build-dir')
    build_dir = None
    if os.path.exists(fx_build_dir_path):
        with open(fx_build_dir_path) as f:
            build_dir = f.read().strip()

    if not build_dir:
        build_dir = 'out/default'

    return os.path.join(fuchsia_dir, build_dir)


class CommandLauncher(object):
    """Helper class used to launch external commands."""

    def __init__(self, cwd, log_file, prefix_args=[]):
        self._cwd = cwd
        self._log_file = log_file
        self._prefix_args = prefix_args

    def run(self, args):
        """Run command, write output to logfile. Raise exception on error."""
        subprocess.check_call(
            self._prefix_args + args,
            stdout=self._log_file,
            stderr=subprocess.STDOUT,
            cwd=self._cwd,
            text=True)

    def get_output(self, args):
        """Run command, return output as string, Raise exception on error."""
        args = self._prefix_args + args
        ret = subprocess.run(
            args, capture_output=True, cwd=self._cwd, text=True)
        if ret.returncode != 0:
            print(
                'ERROR: Received returncode=%d when trying to run command\n  %s\nError output:\n%s'
                % (
                    ret.returncode, ' '.join(
                        shlex.quote(arg) for arg in args), ret.stderr),
                file=sys.stderr)
            ret.check_returncode()

        return ret.stdout


def check_update_workspace_script(
        fuchsia_dir, update_workspace_cmd, cmd_launcher):
    """Check the behavior of the update-workspace.py script!"""

    def get_update_output():
        return cmd_launcher.get_output(update_workspace_cmd)

    # Calling the update script a second time should not trigger an update.
    out = get_update_output()
    assert not out, 'Unexpected workspace update!'

    # Adding a top-level file entry to FUCHSIA_DIR should trigger an update.
    touched_file = os.path.join(
        fuchsia_dir, 'touched-file-for-tests-please-remove')
    with open(touched_file, 'w') as f:
        f.write('0')

    try:
        out = get_update_output()
        assert out, 'Expected workspace update after adding FUCHSIA_DIR file!'

        # Then calling the script again should do nothing.
        out = get_update_output()
        assert not out, 'Unexpected workspace update after FUCHSIA_DIR addition!'
    finally:
        os.remove(touched_file)

    # Removing a top-level file entry from FUCHSIA_DIR should trigger an update.
    out = get_update_output()
    assert out, 'Expected workspace update after removing FUCHSIA_DIR file!'

    # But not calling the script again after that.
    out = get_update_output()
    assert not out, 'Unexpected workspace update after FUCHSIA_DIR removal!'

    # Touching a BUILD.gn file should trigger an update (of the Ninja build plan).
    os.utime(os.path.join(fuchsia_dir, 'build', 'bazel', 'BUILD.gn'))
    out = get_update_output()
    assert out, 'Expected workspace update after touching BUILD.gn file!'


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument(
        '--fuchsia-dir', help='Specify top-level Fuchsia directory.')

    parser.add_argument(
        "--fuchsia-build-dir",
        help='Specify build output directory (auto-detected by default).')

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
        message = '[test-all] ' + message
        if log_file:
            print(message, file=log_file, flush=True)
        if not args.quiet:
            print(message, flush=True)

    if log_file:
        log('Logging enabled, use: tail -f ' + log_file_path)

    log('Using Fuchsia root directory: ' + fuchsia_dir)

    build_dir = args.fuchsia_build_dir
    if not build_dir:
        build_dir = get_fx_build_dir(fuchsia_dir)

    build_dir = os.path.abspath(build_dir)
    log('Using build directory: ' + build_dir)

    if not os.path.exists(build_dir):
        print(
            'ERROR: Missing build directory, did you call `fx set`?: ' +
            build_dir,
            file=sys.stderr)
        return 1

    command_launcher = CommandLauncher(fuchsia_dir, log_file)

    # Path to 'fx' script, relative to fuchsia_dir.
    fx_script = 'scripts/fx'

    def run_fx_command(args):
        command_launcher.run([fx_script] + args)

    def run_bazel_command(args):
        run_fx_command(['bazel'] + args)

    def get_fx_command_output(args):
        return command_launcher.get_output([fx_script] + args)

    def get_bazel_command_output(args):
        return get_fx_command_output(['bazel'] + args)

    # Preparation step
    if args.skip_prepare:
        log('Skipping preparation step due to --skip-prepare.')
    else:
        log('Preparing Fuchsia checkout at: ' + fuchsia_dir)
        command_launcher.run(
            [
                os.path.join(_SCRIPT_DIR, 'prepare-fuchsia-checkout.py'),
                '--fuchsia-dir', fuchsia_dir
            ])

    # Clean step
    if args.skip_clean:
        log('Skipping cleanup step due to --skip-clean.')
    else:
        log('Cleaning current output directory.')
        run_fx_command(['clean'])
        log('Regenerating GN outputs.')
        run_fx_command(['gen'])

    log('Generating bazel workspace and repositories.')
    update_workspace_cmd = [
        os.path.join(_SCRIPT_DIR, 'update-workspace.py'),
        '--fuchsia-dir',
        fuchsia_dir,
    ]
    command_launcher.run(update_workspace_cmd)

    # Prepare bazel wrapper script invocations.
    bazel_script = os.path.join(build_dir, 'gen', 'build', 'bazel', 'bazel')
    assert os.path.exists(bazel_script), (
        'Bazel script does not exist: ' + bazel_script)

    # Verify that adding or removinf files from FUCHSIA_DIR invokes a regeneration.
    log('Checking behavior of update-workspace.py script.')
    check_update_workspace_script(
        fuchsia_dir, update_workspace_cmd, command_launcher)

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

        output = get_bazel_command_output(['query'] + query_patterns)
        try:
            output = get_bazel_command_output(['query'] + query_patterns)
        except subprocess.CalledProcessError:
            return False

        if output != expected_output:
            log('ERROR: Unexpectedoutput for %s query:' % name)
            log(
                'ACTUAL [[[\n%s\n]]] EXPECTED [[[\n%s\n]]]' %
                (output, expected_output))
            return False

        return True

    log('bazel query check (before Ninja build)')
    # NOTE: This requires the previous step to generate the Bazel workspace.
    # However, no Ninja outputs have been generated so far.
    if not check_test_query('pre_ninja_queries', os.path.join(
            args.fuchsia_dir, 'build/bazel/tests/bazel_query_before_ninja')):
        return 1

    # NOTE: this requires Ninja outputs to be properly generated.
    # See note in build/bazel/tests/bazel_inputs_resource_directory/BUILD.bazel
    log("Generating @legacy_ninja_build_outputs repository")
    run_fx_command(
        ['build', '-d', 'explain', 'build/bazel:legacy_ninja_build_outputs'])

    log('bazel_inputs_resource_directory() check.')
    if not check_test_query('bazel_input_resource_directory', os.path.join(
            args.fuchsia_dir,
            'build/bazel/tests/bazel_input_resource_directory')):
        return 1

    log('Checking `fx bazel run //build/bazel/tests/hello_world`.')
    # This builds and runs a simple hello_world host binary using the
    # @prebuilt_clang toolchain (which differs from sdk-integration's
    # @fuchsia_clang that can only generate Fuchsia binaries so far.
    run_bazel_command(['run', '//build/bazel/tests/hello_world'])

    log('bazel_build_action() checks.')
    # This verifies that bazel_build_action() works properly, i.e. that
    # it invokes a Bazel build command that takes inputs from a
    # @legacy_ninja_build_outputs filegroup(), which generates the
    # expected output, copied to the appropriate Ninja output directory
    # location.
    run_fx_command(['build', 'build/bazel/tests/build_action'])

    log('Done!')
    return 0


if __name__ == '__main__':
    sys.exit(main())
