#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
_MAIN_HELP = """Wraps a Rust compile command for remote execution (reclient, RBE).

Given a Rust compile command, this script
1) identifies all inputs needed to execute the command hermetically (remotely).
2) identifies all outputs to be retrieved from remote execution.
3) composes a `rewrapper` (reclient) command to request remote execution.
   This includes downloading remotely produced artifacts.
4) forwards stdout/stderr back to the local environment

This script was ported over from bin/rbe/rustc-remote-wrapper.sh.

Usage:
  rustc_remote_wrapper.py [options] -- RUST-COMPILE-COMMAND

Options:
  --help | -h : print help and exit
  --local : run the command locally
  --dry-run : print diagnostics and exit without running
  --verbose : print additional diagnostics
  --fsatrace : trace file access (works locally and remotely)

  All unknown options are forwarded to rewrapper.

The RUST-COMPILE-COMMAND supports the following pseudo-flags, which are
filtered out prior to execution, and forwarded to rewrapper:

  --remote-disable : same as --local
  --remote-inputs FILE,... : forwarded as --inputs to rewrapper
  --remote-outputs FILE,... : forwarded as --output_files to rewrapper
  --remote-flag OPT : forwarded as OPT (can be flag) to rewrapper

This allows rustflags in GN to influence remote execution parameters.
"""

import argparse
import os
import sys
from typing import Iterable, Sequence, Tuple

# Global constants
_ARGV = sys.argv
_SCRIPT_PATH = _ARGV[0]
_SCRIPT_DIR = os.path.dirname(_SCRIPT_PATH)

# This is the script that eventually calls 'rewrapper' (reclient).
_GENERIC_REMOTE_ACTION_WRAPPER = os.path.join(
    _SCRIPT_DIR, 'fuchsia-rbe-action.sh')

# This script lives under 'build/rbe', so the path to the root is '../..'.
_DEFAULT_PROJECT_ROOT = os.path.realpath(os.path.join(_SCRIPT_DIR, '..', '..'))

# This is the relative path to the build output dir from the project root dir.
_BUILD_SUBDIR = os.path.relpath(os.curdir, start=_DEFAULT_PROJECT_ROOT)

# This is the relative path to the project root dir from the build output dir.
_PROJECT_ROOT_REL = os.path.relpath(_DEFAULT_PROJECT_ROOT, start=os.curdir)

# This is a known path where remote execution occurs.
_REMOTE_PROJECT_ROOT = '/b/f/w'

# Use this env both locally and remotely.
_ENV = '/usr/bin/env'

_CHECK_DETERMINISM_COMMAND = [
    os.path.join(_DEFAULT_PROJECT_ROOT, 'build', 'tracer', 'output_cacher.py'),
    '--check-repeatability',
]

# The path to the prebuilt fsatrace in Fuchsia's project tree.
_FSATRACE_PATH = os.path.join(
    _PROJECT_ROOT_REL, 'prebuilt', 'fsatrace', 'fsatrace')

_DETAIL_DIFF = os.path.join(_SCRIPT_DIR, 'detail-diff.sh')


def apply_remote_flags_from_pseudo_flags(
        main_config: argparse.Namespace, command_params: argparse.Namespace):
    """Apply some flags from the command to the rewrapper configuration.

    Args:
      main_config: main configuration (modified-by-reference).
      command_params: command parameters inferred from parsing the command.
    """
    if command_params.remote_disable:
        main_config.local = True


def parse_main_args(
        command: Sequence[str]) -> Tuple[argparse.Namespace, Sequence[str]]:
    """Scan a the main args for parameters.

    Interface matches that of argparse.ArgumentParser.parse_known_args().
    All unhandled tokens are intended to be forwarded to rewrapper.

    Args:
      command: command, like sys.argv

    Returns:
      A namespace struct with parameters, and a sequence of unhandled tokens.
    """
    params = argparse.Namespace(
        help=None,
        local=False,
        dry_run=False,
        verbose=False,
        fsatrace=False,
        command=[],  # compile command
    )
    forward_to_rewrapper = []

    opt_arg_func = None
    for i, token in enumerate(command):
        # Handle detached --option argument
        if opt_arg_func is not None:
            opt_arg_func(token)
            opt_arg_func = None
            continue

        opt, sep, arg = token.partition('=')

        if token in {'--help', '-h'}:
            params.help = _MAIN_HELP
            break
        if token == '--local':
            params.local = True
        elif token == '--dry-run':
            params.dry_run = True
        elif token == '--verbose':
            params.verbose = True
        elif token == '--fsatrace':
            params.fsatrace = True
        elif token == '--':  # stop option processing
            params.command = command[i + 1:]
            break
        # TODO: if needed, add --project-root override
        # TODO: if needed, add --source override
        # TODO: if needed, add --depfile override
        else:
            forward_to_rewrapper.append(token)

    return params, forward_to_rewrapper


def filter_compile_command(
        command: Sequence[str]) -> Tuple[argparse.Namespace, Sequence[str]]:
    """Scan a command for remote execution parameters, filter out pseudo-flags.

    Interface matches that of argparse.ArgumentParser.parse_known_args().

    Args:
      command: command to scan

    Returns:
      a namespace struct with parameters, and a filtered version of the
      command to remotely execute (with pseudo-flags removed).
    """
    params = argparse.Namespace(
        remote_disable=False,
        remote_inputs=[],
        remote_outputs=[],
        remote_flags=[],
    )
    forward_as_compile_command = []

    opt_arg_func = None
    for token in command:
        # Handle detached --option argument
        if opt_arg_func is not None:
            opt_arg_func(token)
            opt_arg_func = None
            continue

        opt, sep, arg = token.partition('=')

        if token == '--remote-disable':
            params.remote_disable = True
        elif token == '--remote-inputs':
            opt_arg_func = lambda x: setattr(params, 'remote_inputs', x)
        elif opt == '--remote-inputs' and sep == '=':
            params.remote_inputs = arg
        elif token == '--remote-outputs':
            opt_arg_func = lambda x: setattr(params, 'remote_outputs', x)
        elif opt == '--remote-outputs' and sep == '=':
            params.remote_outputs = arg
        elif token == '--remote-flag':
            opt_arg_func = lambda x: params.remote_flags.append(x)
        elif opt == '--remote-flag' and sep == '=':
            params.remote_flags.append(arg)
        else:
            forward_as_compile_command.append(token)

    return params, forward_as_compile_command


# string.removeprefix() only appeared in python 3.9
def remove_dot_slash_prefix(text: str) -> str:
    if text.startswith('./'):
        return text[2:]
    return text


def parse_rust_compile_command(
        compile_command: Sequence[str]) -> argparse.Namespace:
    """Scans a Rust compile command for remote execution parameters.

    Args:
      compile_command: the full (local) Rust compile command, which
        may contain environment variable prefixes.

    Returns:
      A namespace of variables containing remote execution information.
    """
    params = argparse.Namespace(
        depfile=None,
        dep_only_command=[_ENV],  # a modified copy of compile_command
    )
    opt_arg_func = None
    for token in compile_command:
        if opt_arg_func is not None:
            opt_arg_func(token)
            opt_arg_func = None
            continue

        opt, sep, arg = token.partition('=')

        # Create a modified copy of the compile command that will be
        # used to only generate a depfile.
        # Rewrite the --emit token to do exactly this, ignoring
        # all other requested emit outputs.
        if opt == '--emit' and sep == '=':
            emit_args = arg.split(',')
            for emit_arg in emit_args:
                emit_key, emit_sep, emit_value = emit_arg.partition('=')
                if emit_key == 'dep-info' and emit_sep == '=':
                    params.depfile = remove_dot_slash_prefix(emit_value)

            # Tell rustc to report all transitive *library* dependencies,
            # not just the sources, because these all need to be uploaded.
            # This includes (prebuilt) system libraries as well.
            # TODO(https://fxbug.dev/78292): this -Z flag is not known to be stable yet.
            params.dep_only_command += [
                '-Zbinary-dep-depinfo',
                f'--emit=dep-info={params.depfile}.nolink'
            ]
            continue

        # By default, copy over most tokens for depfile generation.
        params.dep_only_command.append(token)

    return params


def main(argv: Sequence[str]):
    # Parse flags, and forward all unhandled flags to rewrapper.
    main_config, forwarded_rewrapper_args = parse_main_args(
        argv[1:])  # drop argv[0], which is this script

    # Exit on --help.
    if main_config.help is not None:
        print(main_config.help)
        return 0

    # The command to run remotely is in main_config.command.
    # Remove pseudo flags from the remote command.
    remote_params, filtered_command = filter_compile_command(
        main_config.command)
    forwarded_rewrapper_args.extend(remote_params.remote_flags)

    # Import some remote parameters back to the main_config.
    apply_remote_flags_from_pseudo_flags(main_config, remote_params)

    compile_params = parse_rust_compile_command(filtered_command)

    # TODO: infer inputs and outputs for remote execution
    # Use the dep-scanning command from compile_params.dep_only_command

    # TODO: construct remote execution command and run it
    # TODO: post-execution diagnostics

    return 0


if __name__ == "__main__":
    sys.exit(main(_ARGV))
