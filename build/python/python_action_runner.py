#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess


def main():
    parser = argparse.ArgumentParser(
        'Execute a python script as an action with a custom environment')

    parser.add_argument(
        '--module_path',
        help='List of python module search paths in standard PATH notation')

    parser.add_argument(
        '--script',
        help='Path to the script to run',
        required=True,
    )

    parser.add_argument(
        'args', help='List of arguments for the script', nargs='*', default=[])

    args = parser.parse_args()

    cmd_env = os.environ
    if args.module_path is not None:
        # Add the module lookup path to the environment for the script
        cmd_env['PYTHONPATH'] = args.module_path

        # Tell the interpreter not write out cached bytecode for modules
        cmd_env['PYTHONDONTWRITEBYTECODE'] = "true"

    cmd = [args.script]
    cmd.extend(args.args)

    subprocess.run(cmd, env=cmd_env, check=True)


if __name__ == "__main__":
    main()
