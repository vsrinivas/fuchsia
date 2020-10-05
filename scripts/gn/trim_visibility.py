#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Example usage:
$ fx set ...
$ scripts/gn/trim_visibility.py\ --target="//build/config:Wno-conversion"
"""

import argparse
import itertools
import os
import re
import subprocess
import sys
import unittest

TARGET_DIR_PART = re.compile(r"\/\/([\w\-_]*(?:\/[\w\-_]+)*)")


def run_command(command):
    return subprocess.check_output(
        command, stderr=subprocess.STDOUT, encoding="utf8")


# TODO(shayba): consider supporting local labels (not just absolutes)
def target_to_dir(target):
    """Returns likely directory path for a target's BUILD.gn file."""
    m = TARGET_DIR_PART.match(target)
    return m.group(1)


# TODO(shayba): consider supporting local labels (not just absolutes)
def is_visible_to(target, visibility):
    """Returns whether dst_target is visible to src_target."""
    target_dirs, _, target_label = target.partition(":")
    visibility_dirs, _, visibility_label = visibility.partition(":")
    for target_dir, visibility_dir in itertools.zip_longest(
            target_dirs.split("/"), visibility_dirs.split("/")):
        if visibility_dir == "*":
            return True
        if target_dir != visibility_dir:
            return False
    if visibility_label == "*":
        return True
    return target_label == visibility_label


def main():
    parser = argparse.ArgumentParser(
        description=
        "Prints which of a target's given visibility list entries are actually needed."
    )
    parser.add_argument("--target", help="Target with visibility")
    parser.add_argument(
        "--verbose",
        action="store_true",
        default=False,
        help="Print progress and stats")
    args = parser.parse_args()

    if not args.target:
        return unittest.main()

    def verbose(*vargs, **kwargs):
        if args.verbose:
            print(*vargs, **kwargs)

    verbose("Getting visibility list...")
    target_visibility = (
        run_command(
            ["fx", "gn", "desc", "out/default", args.target,
             "visibility"]).strip().splitlines())
    verbose(f"Found {len(target_visibility)} elements in list.")

    # Unfortunately we can't rely on `gn refs` to find all references to the
    # target if it's a config.
    # https://bugs.chromium.org/p/gn/issues/detail?id=203
    # Instead we hope that we can translate visibility labels to BUILD.gn files
    # correctly, and then wing it by looking for any valid *.gn file under the
    # given subdirectory.
    # This approach is not expected to be accurate.

    used_visibility = set()
    for vis in target_visibility:
        found_usage = False
        for root, _, files in os.walk(target_to_dir(vis)):
            for filename in files:
                if os.path.splitext(filename)[1] == ".gn":
                    if args.target in open(os.path.join(root, filename)).read():
                        used_visibility.add(vis)
                        found_usage = True
                        break
            if found_usage:
                break

    verbose("Used visibility:")
    for used in sorted(used_visibility):
        print(f'"{used}",')

    return 0


class Test(unittest.TestCase):

    def test_target_to_dir(self):

        def expect(target, build_dir):
            self.assertTrue(target_to_dir(target), build_dir)

        expect(r"//build/config:Wno-conversion", "build/config")
        expect(r"//foo/bar/*", "foo/bar")
        expect(r"//foo:bar", "foo/bar")

    def test_is_visible_to(self):

        def is_visible(src, dst):
            self.assertTrue(is_visible_to(src, dst))

        def not_visible(src, dst):
            self.assertFalse(is_visible_to(src, dst))

        is_visible("//foo:bar", "*")
        is_visible("//foo:bar", "//foo/*")
        is_visible("//foo:bar", "//foo:bar")
        is_visible("//foo:bar", "//foo:*")
        not_visible("//foo:bar", "//foo:baz")
        not_visible("//foo:bar", "//foo/baz")
        not_visible("//foo/bar:baz", "//foo/bar")


if __name__ == "__main__":
    sys.exit(main())
