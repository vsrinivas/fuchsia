#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Process build landmines. If it detects that the build should be clobbered,
it will run `gn clean`.

A landmine is tripped when a machine checks out a different revision and
there is a diff between $BUILD_DIR/.landmines and the output of get_landmines.py.
"""

import argparse
import os
import pathlib
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="Process build landmines.")
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

    get_landmines_script = args.checkout_dir / "build" / "landmines" / "get_landmines.py"
    existing_landmines_path = args.build_dir / ".landmines"

    # create an empty landmines file if it doesn't exist
    if not existing_landmines_path.exists():
        with open(existing_landmines_path, "w") as f:
            if args.verbose:
                print("created empty landmine file")
    # read existing landmines
    if args.verbose:
        print("reading existing build dir's landmines")
    with open(existing_landmines_path, "r") as f:
        existing_landmines = f.read()

    # execute new landmines script using the same interpreter as us
    if args.verbose:
        print("generating current landmines from script")
    current_landmines = subprocess.run(
        [sys.executable, get_landmines_script],
        stdout=subprocess.PIPE,
        text=True).stdout

    # clobber if needed
    if existing_landmines != current_landmines:
        if args.verbose:
            print(f"cleaning build and updating `{existing_landmines_path}`")
        with open(existing_landmines_path, "w") as f:
            f.write(current_landmines)
        subprocess.run([args.gn_bin, "clean", args.build_dir])
    else:
        if args.verbose:
            print("landmines up-to-date, not clobbering")

    return 0


if __name__ == '__main__':
    sys.exit(main())
