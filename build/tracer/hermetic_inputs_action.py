#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Launch a command and generate a Ninja depfile for it from an input hermetic inputs file."""

import argparse
import os
import subprocess
import sys

from typing import AbstractSet, Any, Callable, Collection, FrozenSet, Iterable, Optional, Sequence, TextIO, Tuple


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--hermetic-inputs-file',
        required=True,
        help="Path to input hermetic inputs file")
    parser.add_argument(
        '--outputs',
        required=True,
        nargs='*',
        help='Action outputs, to be listed in generated depfile')
    parser.add_argument(
        '--depfile', required=True, help='Path to output depfile')
    parser.add_argument(
        'command', nargs=argparse.REMAINDER, help='Action command')

    args = parser.parse_args()

    # Read implicit inputs from file.
    with open(args.hermetic_inputs_file) as f:
        implicit_inputs = [l.rstrip() for l in f.readlines()]

    # Read command, and remove initial -- if it is found.
    cmd_args = args.command
    if cmd_args[0] == '--':
        cmd_args = cmd_args[1:]

    # If command is a Python script, invoke it through the same interpreter.
    tool = cmd_args[0]
    if tool.endswith(('.py', '.pyz')):
        cmd_args = [sys.executable, '-S'] + cmd_args

    # Run the command.
    try:
        subprocess.check_call(cmd_args)
    except subprocess.CalledProcessError as exc:
        # Simply forward the exit code instead of raising an exception to avoid
        # polluting every build error message with a generic stack trace from
        # this script.
        return exc.returncode

    # Generate the depfile.
    with open(args.depfile, 'w') as f:
        f.write(
            '%s: %s\n' % (' '.join(args.outputs), ' '.join(implicit_inputs)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
