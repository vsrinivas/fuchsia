#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Run a bazel command to ensure the @fuchsia_sdk repository is populated."""

import argparse
import errno
import filecmp
import os
import shlex
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--bazel-launcher',
        required=True,
        help='Path to Bazel launcher script.')
    parser.add_argument(
        '--output-symlink',
        required=True,
        help='Symlink to create pointing to the repository\'s directory')

    args = parser.parse_args()

    if not os.path.exists(args.bazel_launcher):
        return parser.error(
            'Bazel launcher does not exist: %s' % args.bazel_launcher)

    cmd = [
        args.bazel_launcher,
        'build',
        '--verbose_failures',
        '@fuchsia_sdk//:BUILD.bazel',
    ]
    ret = subprocess.run(cmd)
    if ret.returncode != 0:
        print(
            'ERROR when calling Bazel. To reproduce, run this in the Ninja output directory:\n\n  %s\n'
            % ' '.join(shlex.quote(c) for c in cmd),
            file=sys.stderr)
        return 1

    # The name of the external repository can vary greatly with BzlMod
    # depending on how it was defined through MODULE.bazel.
    # TODO(digit): When Bazel 6.0 is out, replace this probing with a query
    # that can return the actual path correctly.
    candidates = [
        'fuchsia_sdk',
        'fuchsia_sdk.override',
        'fuchsia_sdk_repositories~fuchsia_sdk',
        'fuchsia_sdk_repositories~fuchsia_sdk~local',
    ]
    topdir = os.path.dirname(args.bazel_launcher)
    fuchsia_sdk_dir = None
    for candidate in candidates:
        candidate_dir = os.path.join(
            topdir, 'output_base', 'external', candidate)
        if os.path.exists(candidate_dir):
            fuchsia_sdk_dir = os.path.abspath(candidate_dir)
            break

    assert fuchsia_sdk_dir, 'Could not find @fuchsia_sdk repository location!!'

    # Re-generate the symlink if its path has changed, or if it does not exist.
    link_path = os.path.abspath(args.output_symlink)
    link_dir = os.path.dirname(link_path)
    if os.path.exists(link_path):
        if os.path.islink(link_path):
            target_path = os.readlink(link_path)
            if not os.path.isabs(target_path):
                target_path = os.path.abspath(
                    os.path.realpath(os.path.join(link_dir, target_path)))

            if target_path == fuchsia_sdk_dir:
                # Path did not change, exit now
                return 0

        os.unlink(link_path)

    target_path = os.path.relpath(fuchsia_sdk_dir, link_dir)
    os.symlink(fuchsia_sdk_dir, link_path)

    # Done!
    return 0


if __name__ == "__main__":
    sys.exit(main())
