#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Test script that invokes `fx create` and compares its output with a set of golden files.
"""

from __future__ import print_function
import argparse
import difflib
import json
import os
import shutil
import subprocess
import sys
import tempfile


class TemporaryDirectory(object):
    """Backport of tempfile.TemporaryDirectory for python 2.7."""

    def __enter__(self):
        self.name = tempfile.mkdtemp()
        return self.name

    def __exit__(self, type, value, traceback):
        shutil.rmtree(self.name)


# Use the python3 TemporaryDirectory if available, otherwise use the fallback impl.
TemporaryDirectory = getattr(tempfile, 'TemporaryDirectory', TemporaryDirectory)


# Prints a diff (if there is one) of the two files, and returns True if there was a difference.
def diff_files(golden, generated):
    diff_exists = False
    with open(golden) as golden_file, open(generated) as generated_file:
        for line in difflib.unified_diff(golden_file.readlines(),
                                         generated_file.readlines(),
                                         fromfile=golden, tofile=generated):
            diff_exists = True
            sys.stderr.write(line)
    return diff_exists


# Finds all files in a directory recursively. The paths include `dir` as a prefix.
def get_files_in_dir(dir):
    for root, dirs, files in os.walk(dir):
        for file in files:
            yield os.path.join(root, file)


def eprint(msg):
    print(msg, file=sys.stderr)


def main():
    script_dir = os.path.dirname(sys.argv[0])

    parser = argparse.ArgumentParser(
        description=
        "Invokes `fx create` and compares its output with a set of golden files."
    )
    parser.add_argument('create_bin', help='path to the create binary')
    parser.add_argument(
        'golden_files', help='path to the JSON file listing all golden files')
    parser.add_argument('project_type', help='project type as per fx create')
    parser.add_argument('project_name', help='project name as per fx create')
    parser.add_argument(
        'create_args',
        nargs=argparse.REMAINDER,
        help='other arguments to `fx create`')
    args = parser.parse_args()

    # Read the set of golden files accessible to this script.
    with open(args.golden_files) as f:
        golden_files = set(json.load(f))

    # Create a temporary directory to house the generated project.
    with TemporaryDirectory() as project_dir:
        args = [
            args.create_bin, args.project_type,
            os.path.join(project_dir, args.project_name)
        ] + args.create_args

        # Call the create tool
        subprocess.check_call(args)

        error = False

        # For each generated file, diff it against its associated golden file.
        for generated_path in get_files_in_dir(project_dir):
            # Strip the tmp dir prefix to get the base path of the generated file.
            base_path = generated_path[len(project_dir) + 1:]
            golden_path = os.path.join(script_dir, base_path)
            try:
                golden_files.remove(golden_path)
            except KeyError:
                error = True
                eprint(
                    'generated file {} missing in golden project'.format(
                        base_path))
                continue
            if diff_files(golden_path, generated_path):
                error = True

        # For each golden file not matched against a generated file, print an error.
        for golden_path in golden_files:
            error = True
            base_path = golden_path[len(script_dir) + 1:]
            eprint(
                'golden file {0} missing in generated project'.format(
                    base_path))

        if error:
            return 1
        return 0


if __name__ == '__main__':
    sys.exit(main())
