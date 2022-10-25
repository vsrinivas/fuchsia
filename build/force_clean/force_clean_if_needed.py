#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Process build "force clean fences". If it detects that the build should be clobbered,
it will run `gn clean`.

A fence is crossed when a machine checks out a different revision and
there is a diff between $BUILD_DIR/.force_clean_fences and the output of get_fences.py.
"""

import argparse
import os
import pathlib
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Process build force-clean fences.")
    parser.add_argument(
        "--gn-bin",
        type=pathlib.Path,
        required=True,
        help="Path to prebuilt GN binary.")
    parser.add_argument(
        "--checkout-dir",
        type=pathlib.Path,
        required=True,
        help="Path to $FUCHSIA_DIR.")
    parser.add_argument(
        "--build-dir",
        type=pathlib.Path,
        required=True,
        help="Path to the root build dir, e.g. $FUCHSIA_DIR/out/default.")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    # check that inputs are valid
    if not args.gn_bin.is_file():
        raise RuntimeError(f"{args.gn_bin} is not a file")
    if not args.checkout_dir.is_dir():
        raise RuntimeError(f"{args.checkout_dir} is not a directory")
    if not args.build_dir.is_dir():
        raise RuntimeError(f"{args.build_dir} is not a directory")

    get_fences_script = args.checkout_dir / "build" / "force_clean" / "get_fences.py"
    existing_fences_path = args.build_dir / ".force_clean_fences"

    # create an empty fences file if it doesn't exist
    if not existing_fences_path.exists():
        if args.verbose:
            print("no fences file found, assuming nothing to clean")
        return 0

    # read existing fences
    if args.verbose:
        print("reading existing build dir's force-clean fences")
    with open(existing_fences_path, "r") as f:
        existing_fences = f.read()

    # execute new fences script using the same interpreter as us
    if args.verbose:
        print("generating current fences from script")
    current_fences = subprocess.run(
        [sys.executable, get_fences_script], stdout=subprocess.PIPE,
        text=True).stdout

    # clobber if needed
    if existing_fences != current_fences:
        if args.verbose:
            print(f"cleaning build and updating `{existing_fences_path}`")
        subprocess.run([args.gn_bin, "clean", args.build_dir])
        with open(existing_fences_path, "w") as f:
            f.write(current_fences)
    else:
        if args.verbose:
            print("force_clean fences up-to-date, not clobbering")

    return 0


if __name__ == '__main__':
    sys.exit(main())
