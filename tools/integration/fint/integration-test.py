#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Script for running fint directly.

This is useful for testing features, such as affected tests analysis, that are
normally only exercised by the infrastructure and not by fx.

For example, if you want to see which tests `fint build` considers affected when
the files "src/foo.cc" and "src/bar.cc" are changed, run:

  ./tools/integration/fint/integration-test.py build -f <fint-params-path> -c src/foo.cc -c src/bar.cc

Where <fint-params-path> is the relative path to a textproto file as chosen by
`fx repro`.

Note that the affected tests analysis is also dependent on the state of the
checkout, so if you're testing to see how fint handles a specific change, you'll
need to cherry-pick that change into your checkout in addition to passing in the
names of the changed files.

If successful, this will print the path to a temporary directory to which fint
has written its outputs. For the `build` command, the outputs will be in a file
named `build_artifacts.json` in that directory.
"""

import argparse
import os
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        'cmd', type=str, help='fint subcommand to run (set or build)')
    parser.add_argument(
        '-f',
        '--fint-params-path',
        help='path to fint params file',
        required=True)
    parser.add_argument(
        '-c',
        '--changed-file',
        action='append',
        dest='changed_files',
        default=[])

    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as inputs_dir:
        execute(args, inputs_dir)


def execute(args, inputs_dir):
    checkout_dir = os.environ['FUCHSIA_DIR']
    os.chdir(checkout_dir)

    artifact_dir = tempfile.mkdtemp()

    build_dir = subprocess.check_output(['fx', 'get-build-dir'],
                                        text=True).strip()

    context_textproto = f'''
checkout_dir: "{checkout_dir}"
build_dir: "{build_dir}"
artifact_dir: "{artifact_dir}"
'''
    for changed_file in args.changed_files:
        context_textproto += f'''
changed_files {{
    path: "{changed_file}"
}}
'''

    context_path = os.path.join(inputs_dir, "context.textproto")
    with open(context_path, 'w') as f:
        f.write(context_textproto)

    proc = subprocess.run(
        [
            'fx', 'go', 'run',
            './tools/integration/fint/cmd/fint',
            '-log-level', 'info',
            args.cmd,
            '-static', args.fint_params_path,
            '-context', context_path,
        ])  # yapf: disable
    if proc.returncode != 0:
        print("Failed to run fint. Make sure you have run `fx setup-go`.")
        sys.exit(1)

    print(f"See {artifact_dir} for outputs.")


if __name__ == '__main__':
    main()
