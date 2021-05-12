#!/usr/bin/env python3
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Test script that invokes `fx create` and compares its output with a set of golden files.
"""

import argparse
import difflib
import json
import os
import shutil
import subprocess
import sys
import tempfile


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
    parser = argparse.ArgumentParser(
        description=
        "Invokes `fx create` and compares its output with a set of golden files."
    )
    parser.add_argument(
        'test_dir', help='path to the test\'s working directory')
    parser.add_argument('create_bin', help='path to the create binary')
    parser.add_argument(
        'golden_files', help='path to the JSON file listing all golden files')
    parser.add_argument('project_type', help='project type as per fx create')
    parser.add_argument('project_subtype', help='project subtype as per fx create')
    parser.add_argument('project_name', help='project name as per fx create')
    parser.add_argument(
        'create_args',
        nargs=argparse.REMAINDER,
        help='other arguments to `fx create`')
    proc_args = parser.parse_args()

    # Read the set of golden files accessible to this script.
    with open(proc_args.golden_files) as f:
        golden_files = set(json.load(f))

    # Create a temporary directory to house the generated project.
    with tempfile.TemporaryDirectory() as project_dir:
        args = [
            proc_args.create_bin,
            proc_args.project_type, proc_args.project_subtype,
            "--path", os.path.join(project_dir, proc_args.project_name)
        ] + proc_args.create_args

        # Call the create tool
        subprocess.check_call(args)

        error = False

        # For each generated file, diff it against its associated golden file.
        for generated_path in get_files_in_dir(project_dir):
            # Strip the tmp dir prefix to get the base path of the generated file.
            base_path = generated_path[len(project_dir) + 1:]
            golden_path = os.path.join(proc_args.test_dir, base_path)
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
            sys.exit(1)


# The python_host_test build rule calls `unittest.main`.
# Since this is not a unittest (and can't be since it takes command line arguments),
# we pretend our main is unittest's main.

unittest = sys.modules[__name__]

if __name__ == '__main__':
    unittest.main()
