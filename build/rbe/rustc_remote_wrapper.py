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

_CHECK_DETERMINISM_COMMAND = [
    os.path.join(_DEFAULT_PROJECT_ROOT, 'build', 'tracer', 'output_cacher.py'),
    '--check-repeatability',
]

# The path to the prebuilt fsatrace in Fuchsia's project tree.
_FSATRACE_PATH = os.path.join(
    _PROJECT_ROOT_REL, 'prebuilt', 'fsatrace', 'fsatrace')

_DETAIL_DIFF = os.path.join(_SCRIPT_DIR, 'detail-diff.sh')


def apply_remote_flags_from_pseudo_flags(main_config, command_params):
    """Apply some flags from the command to the rewrapper configuration.

    Args:
      main_config: main configuration (modified-by-reference).
      command_params: command parameters inferred from parsing the command.
    """
    if command_params.remote_disable:
        main_config.local = True


def remove_command_pseudo_flags(command: Iterable[str]) -> Iterable[str]:
    """Remove pseudo flags from the command.

    These pseudo flags exist to provide a means of influencing remote
    execution in the position where normal tool flags appear to make
    up for the inability to pass the same information to the wrapper
    prefix in GN.

    Semantic handling of these flags is handled in compile_command_parser().
    """
    ignore_next_token = False
    for token in command:
        if ignore_next_token:
            ignore_next_token = False
            continue
        elif token == '--remote-disable':
            pass
        elif token == '--remote-inputs':
            ignore_next_token = True
        elif token.startswith('--remote-inputs='):
            pass
        elif token == '--remote-outputs':
            ignore_next_token = True
        elif token.startswith('--remote-outputs='):
            pass
        elif token == '--remote-flag':
            ignore_next_token = True
        elif token.startswith('--remote-flag='):
            pass
        else:
            yield token


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


def parse_compile_command(
        command: Sequence[str]) -> Tuple[argparse.Namespace, Sequence[str]]:
    """Scan a compile command for parameters.

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

    # Workaround inability to use assign statement inside lambda.
    def set_params_attr(attr: str, value):
        setattr(params, attr, value)

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
            opt_arg_func = lambda x: set_params_attr('remote_inputs', x)
        elif opt == '--remote-inputs' and sep == '=':
            params.remote_inputs = arg
        elif token == '--remote-outputs':
            opt_arg_func = lambda x: set_params_attr('remote_outputs', x)
        elif opt == '--remote-outputs' and sep == '=':
            params.remote_outputs = arg
        elif token == '--remote-flag':
            opt_arg_func = lambda x: params.remote_flags.append(x)
        elif opt == '--remote-flag' and sep == '=':
            params.remote_flags.append(arg)
        else:
            forward_as_compile_command.append(token)

    return params, forward_as_compile_command


def main(argv: Sequence[str]):
    # Parse flags, and forward all unhandled flags to rewrapper.
    main_config, forwarded_rewrapper_args = parse_main_args(
        argv[1:])  # drop argv[0], which is this script

    # Exit on --help.
    if main_config.help is not None:
        print(main_config.help)
        return 0

    # The command to run remotely is in main_config.command.
    command_params = parse_compile_command(main_config.command)
    forwarded_rewrapper_args.extend(command_params.remote_flags)

    # Import some remote parameters back to the main_config.
    apply_remote_flags_from_pseudo_flags(main_config, command_params)

    # Remove pseudo flags from the remote command.
    filtered_command = remove_command_pseudo_flags(main_config.command)

    # TODO: infer inputs and outputs for remote execution
    # TODO: construct remote execution command and run it
    # TODO: post-execution diagnostics

    return 0


if __name__ == "__main__":
    sys.exit(main(_ARGV))
