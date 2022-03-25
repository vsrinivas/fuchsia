#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Wraps a Rust compile command for remote execution (reclient, RBE).

Given a Rust compile command, this script
1) identifies all inputs needed to execute the command hermetically (remotely).
2) identifies all outputs to be retrieved from remote execution.
3) composes a `rewrapper` (reclient) command to request remote execution.
   This includes downloading remotely produced artifacts.
4) forwards stdout/stderr back to the local environment

This script was ported over from bin/rbe/rustc-remote-wrapper.sh.
"""

import argparse
import os
import sys
from typing import Sequence

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


def main_arg_parser() -> argparse.ArgumentParser:
    """Construct the argument parser, called by main()."""
    parser = argparse.ArgumentParser(
        description="Wraps a Rust command for remote execution",
        argument_default=[],
    )
    parser.add_argument(
        '--dry-run',
        action='store_true',
        default=False,
        help='Stop before remote execution, and display some diagnostics.')
    parser.add_argument(
        '--local',
        action='store_true',
        default=False,
        help="Run the original command locally, not remotely.",
    )
    parser.add_argument(
        '--verbose',
        action='store_true',
        default=False,
        help="Display debug information during processing.",
    )
    parser.add_argument(
        '--fsatrace',
        action='store_true',
        default=False,
        help=
        "Trace file access during (local or remote) execution when the path to an fsatrace binary is given.",
    )
    # TODO: if needed, add --project-root override
    # TODO: if needed, add --source override
    # TODO: if needed, add --depfile override

    # Positional args are the command and arguments to run.
    # '--' also forces remaining args to treated positionally,
    # and is recommended for visual separation.
    parser.add_argument('command', nargs="*", help="The command to execute")
    return parser


def main(argv: Sequence[str]):
    parser = main_arg_parser()
    args = parser.parse_args(argv[1:])  # drop argv[0], which is this script
    # The command to run is in args.command

    # TODO: infer inputs and outputs for remote execution
    # TODO: construct remote execution command and run it
    # TODO: post-execution diagnostics

    return 0


if __name__ == "__main__":
    sys.exit(main(_ARGV))
