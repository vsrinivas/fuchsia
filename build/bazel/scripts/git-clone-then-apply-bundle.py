#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Clone the content of a git repository, then apply a git bundle to it."""
import argparse
import os
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--dst-dir', required=True, help='Destination directory.')
    parser.add_argument(
        '--git-url', required=True, help='Source git repository URL.')
    parser.add_argument(
        '--git-bundle', required=True, help='Path to git bundle to apply.')
    parser.add_argument(
        '--git-bundle-head',
        required=True,
        help='Name of bundle head to checkout and rebase')

    args = parser.parse_args()

    def run_git_cmd(cmd_args):
        subprocess.check_call(['git', '-C', args.dst_dir] + cmd_args)

    git_url = args.git_url
    if '://' not in git_url:
        git_url = os.path.abspath(git_url)

    os.makedirs(args.dst_dir, exist_ok=True)
    run_git_cmd(['init'])
    run_git_cmd(['remote', 'add', 'source', git_url])
    run_git_cmd(['remote', 'add', 'bundle', os.path.abspath(args.git_bundle)])
    run_git_cmd(
        ['fetch', '--quiet', '--tags', '--shallow-exclude=JIRI_HEAD', 'source'])
    run_git_cmd(['fetch', '--quiet', 'bundle'])
    run_git_cmd(['checkout', '-b', 'main', 'bundle/' + args.git_bundle_head])

    return 0


if __name__ == "__main__":
    sys.exit(main())
