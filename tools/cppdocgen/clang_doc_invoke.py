#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import tempfile
import subprocess
import sys
import zipfile

# Wraps a clang-doc invocation for the hermetic build.
#
# Clang-doc generates a directory of output. Since the output file list isn't knowable in advance,
# we need to generate a single archive of the output. This script creates a new empty temp
# directory, runs clang-doc to generate the output, zips that directory to the output, and deletes
# the temporary directory.
#
# Positional arguments are passed directly to clang-doc. Prefix with a "--" to separate them from
# the parameters to this script. Do not include the --output parameter, this will be synthesized
# by this script to point to the temp directory.
#
# Example:
#
#    clang_doc_invoke.py --clang-doc=prebuilt/x64/clang-doc --temp-dir-parent=/tmp
#        --out.zip=output.zip -- --format=yaml --executor=all-TUs --filter=foo.cc
#                                🠭 Args to clang-doc start here.


def main():
    parser = argparse.ArgumentParser(
        'Runs clang-doc in a temporary directory and zips the output.\n')
    parser.add_argument(
        '--clang-doc', help='Path to clang-doc binary.', required=True)
    parser.add_argument(
        '--temp-dir-parent',
        help='Parent directory of the unique temp dir to use.',
        required=True)
    parser.add_argument(
        '--out-zip',
        help='Name of the .zip file to create from the clang-doc output.',
        required=True)
    parser.add_argument(
        'clang_doc_args',
        help='Arguments to pass to clang-doc',
        nargs='+',
        metavar='clang-doc-args')
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(dir=args.temp_dir_parent) as temp_dir:
        completed = subprocess.run(
            [args.clang_doc, '--output', temp_dir] + args.clang_doc_args,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)

        file_count = 0
        with zipfile.ZipFile(args.out_zip, 'w') as outfile:
            for root, dirs, files in os.walk(temp_dir):
                for file in files:
                    file_path = os.path.join(root, file)
                    outfile.write(
                        file_path, arcname=os.path.relpath(file_path, temp_dir))
                    file_count = file_count + 1

        if file_count == 0:
            # Clang-doc doesn't always return nonzero on failure so this is often the failure case.
            print("clang-doc produced no files, failing.")
            print(
                "  This is normally because the target is not in the 'default' build"
            )
            print("  and the fix is to add the cpp_docgen target to your 'universe'.")
            print("\nclang-doc output:\n")
            print(bytes.decode(completed.stdout))
            return 1
        return 0


if __name__ == '__main__':
    sys.exit(main())
