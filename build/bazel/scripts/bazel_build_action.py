#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"Run a bazel build command from Ninja. See bazel_build_action() for details."

import argparse
import errno
import filecmp
import os
import shutil
import subprocess
import sys


def copy_file_if_changed(src_path, dst_path):
    # NOTE: For some reason, filecmp.cmp() will return True if
    # dst_path does not exist, even if src_path is not empty!?
    if os.path.exists(dst_path) and filecmp.cmp(src_path, dst_path,
                                                shallow=False):
        return
    if os.path.exists(dst_path):
        os.remove(dst_path)
    try:
        os.link(src_path, dst_path)
    except OSError as e:
        if e.errno == errno.EXDEV:
            # Cross-device link, simple copy.
            shutil.copy2(src_path, dst_path)
        else:
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--bazel-launcher',
        required=True,
        help='Path to Bazel launcher script.')
    parser.add_argument(
        '--workspace-dir', required=True, help='Bazel workspace path')
    parser.add_argument(
        '--bazel-targets',
        action='append',
        default=[],
        help='List of bazel target patterns to build.')
    parser.add_argument(
        '--bazel-outputs',
        default=[],
        nargs='*',
        help='Bazel output paths, relative to bazel-bin/.')
    parser.add_argument(
        '--ninja-outputs',
        default=[],
        nargs='*',
        help='Ninja output paths relative to current directory.')

    args = parser.parse_args()

    if not args.bazel_targets:
        return parser.error('A least one --bazel-targets value is needed!')

    if not args.bazel_outputs:
        return parser.error('At least one --bazel-outputs value is needed!')

    if not args.ninja_outputs:
        return parser.error('At least one --ninja-outputs value is needed!')

    if len(args.bazel_outputs) != len(args.ninja_outputs):
        return parser.error(
            'The --bazel-outputs and --ninja-outputs lists must have the same size!'
        )

    if not os.path.exists(args.workspace_dir):
        return parser.error(
            'Workspace directory does not exist: %s' % args.workspace_dir)

    if not os.path.exists(args.bazel_launcher):
        return parser.error(
            'Bazel launcher does not exist: %s' % args.bazel_launcher)

    cmd = [args.bazel_launcher, 'build'] + args.bazel_targets
    subprocess.check_call(cmd)

    for bazel_out, ninja_out in zip(args.bazel_outputs, args.ninja_outputs):
        src_path = os.path.join(args.workspace_dir, 'bazel-bin', bazel_out)
        dst_path = ninja_out
        copy_file_if_changed(src_path, dst_path)

    # Done!
    return 0


if __name__ == "__main__":
    sys.exit(main())
