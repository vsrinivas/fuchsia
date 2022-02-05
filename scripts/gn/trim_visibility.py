#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Example usage:
$ fx set ...
$ scripts/gn/trim_visibility.py --target="//build/config:Wno-conversion"

The output is useful for instance if you have a visibility allowlist for
a deprecation and you'd like to trim it for stale values.
"""

import argparse
import os
import sys
import unittest
import gn_util


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
    target_visibility = gn_util.gn_desc(args.target, "visibility")
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
        for root, _, files in os.walk(gn_util.target_to_dir(vis)):
            for filename in files:
                if os.path.splitext(filename)[1] in (".gn", ".gni"):
                    # This has some false positives, but is better than parsing
                    # GN files or relying on `gn desc` for the target, each of
                    # which is a whole can of worms.
                    if args.target in open(os.path.join(root, filename)).read():
                        used_visibility.add(gn_util.canonicalize_target(vis))
                        found_usage = True
                        break
            if found_usage:
                break

    verbose("Used visibility:")
    for used in sorted(used_visibility):
        print(f'"{used}",')

    return 0


if __name__ == "__main__":
    sys.exit(main())
