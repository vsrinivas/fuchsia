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
import subprocess
import sys
import unittest


def run_command(command):
    return subprocess.check_output(
        command, stderr=subprocess.STDOUT, encoding="utf8")


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

    verbose("Getting refs to target...")
    refs = (
        run_command(["fx", "gn", "refs", "out/default", args.target,
                     "-q"]).strip().splitlines())
    verbose(f"Found {len(refs)} references.")

    used_visibility = set()
    for ref in refs:
        is_visible = False
        for vis in target_visibility:
            if is_visible_to(ref, vis):
                is_visible = True
                used_visibility.add(vis)
                break
        # Note that some refs won't have visibility to the config.
        # This is because they're not directly referencing it, yet they're
        # mistakenly listed by gn as refs.
        # https://bugs.chromium.org/p/gn/issues/detail?id=203
        # If this bug is fixed then the check below should return an error as
        # this is no longer expected.
        if not is_visible:
            verbose(f"{ref} doesn't have visibility to {args.target}")

    verbose("Used visibility:")
    for used in sorted(used_visibility):
        print(f'"{used}",')

    return 0


class Test(unittest.TestCase):

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
