#!/usr/bin/env fuchsia-vendored-python
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate helpdoc reference docs for fx commands and subcommands.

This script calls fx helpdoc and generates reference docs for fx
commands and subcommands.
"""

import sys
import argparse
import os
import subprocess


def run_fx_helpdoc(src_dir, out_path, log_to_file=None):
    fx_bin = os.path.join(src_dir, "scripts/fx")
    if log_to_file:
        log_to_file = open(log_to_file, 'w')
    gen_helpdocs = subprocess.run([fx_bin, "helpdoc", "--archive", out_path], stdout=log_to_file)

    if gen_helpdocs.returncode:
        print(gen_helpdocs.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,  # Prepend help doc with this file's docstring.
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        '-o',
        '--out-path',
        type=str,
        required=True,
        help='Output location where generated docs should go')
    parser.add_argument(
        '-s',
        '--src-dir',
        type=str,
        required=True,
        help='Home location of Fuchsia relative to build')
    parser.add_argument(
        '-l',
        '--log-to-file',
        type=str,
        help='File for logging stdout.')

    args = parser.parse_args()
    run_fx_helpdoc(
        args.src_dir,
        args.out_path,
        args.log_to_file
    )


if __name__ == '__main__':
    sys.exit(main())
