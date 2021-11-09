#!/usr/bin/env fuchsia-vendored-python
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate FIDL reference docs for one or more Fuchsia packages.

This script uses fidldoc which documents FIDL libraries passed in via
a .txt file.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys


def walk_rmtree(directory):
    """Manually remove all subdirectories and files of a directory
    via os.walk instead of using
    shutil.rmtree, to avoid registering spurious reads on stale
    subdirectories. See https://fxbug.dev/74084.

    Args:
        directory: path to directory which should have tree removed.
    """
    for root, dirs, files in os.walk(directory, topdown=False):
        for file in files:
            os.unlink(os.path.join(root, file))
        for dir in dirs:
            full_path = os.path.join(root, dir)
            if os.path.islink(full_path):
                os.unlink(full_path)
            else:
                os.rmdir(full_path)
    os.rmdir(directory)


def read_fidl_packages(build_dir):
    fidldoc = os.path.join(build_dir, 'sdk_fidl_json.json')
    with open(fidldoc, 'r') as fdl_json:
        fidl_pkgs = json.load(fdl_json)
    return [pkg["ir"] for pkg in fidl_pkgs]


def run_fidl_doc(build_dir, out_dir, fidl_files, zipped_result=False):
    fidldoc_path = os.path.join(build_dir, 'host-tools', 'fidldoc')
    out_fidl = os.path.join(out_dir, 'fidldoc')
    gen_fidl = subprocess.run(
        [
            fidldoc_path, "--verbose", "--path", "/reference/fidl/", "--out",
            out_fidl
        ] + fidl_files)

    if gen_fidl.returncode:
        print(gen_fidl.stderr)
        return 1

    if zipped_result:
        shutil.make_archive(
            os.path.join(out_dir, 'fidldoc'),
            'zip',
            root_dir=out_dir,
            base_dir='fidldoc')
        walk_rmtree(out_fidl)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,  # Prepend help doc with this file's docstring.
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        '-z',
        '--zipped-result',
        action='store_true',
        help=(
            'If set will zip output documentation and delete md files.'
            'This is to make the doc generation process hermetic'))
    parser.add_argument(
        '--dep-file', type=argparse.FileType('w'), required=True)
    parser.add_argument(
        '-o',
        '--out-dir',
        type=str,
        required=True,
        help='Output location where generated docs should go')
    parser.add_argument(
        '-b',
        '--build-dir',
        type=str,
        required=True,
        help='Directory location of previously built artifacts')

    args = parser.parse_args()
    input_fidl_files = read_fidl_packages(args.build_dir)
    args.dep_file.write(
        '{}: {}\n'.format(
            os.path.join(args.out_dir, 'fidldoc.zip'),
            ' '.join(input_fidl_files)))
    run_fidl_doc(
        args.build_dir, args.out_dir, input_fidl_files, args.zipped_result)


if __name__ == '__main__':
    sys.exit(main())
