#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### runs rustfmt on a Rust target

import argparse
import os
import platform
import subprocess
import sys

import rust
from rust import ROOT_PATH

sys.path += [os.path.join(ROOT_PATH, "third_party", "pytoml")]
import pytoml as toml


def main():
    parser = argparse.ArgumentParser("Format a rust target")
    parser.add_argument(
        "gn_targets",
        metavar="gn_target",
        nargs="+",
        type=rust.GnTarget,
        help="GN target to format. \
                              Use '.[:target]' to discover the cargo target \
                              for the current directory or use the \
                              absolute path to the target \
                              (relative to $FUCHSIA_DIR). \
                              For example: //garnet/bin/foo/bar:baz.")
    parser.add_argument(
        "-v", "--verbose", action='store_true', help="Show verbose output")
    parser.add_argument("--out-dir", help="Path to the Fuchsia build directory")
    parser.add_argument(
        "-s",
        "--print-sources",
        action='store_true',
        help="Only print sources; do not format")

    args = parser.parse_args()

    build_dir = os.path.abspath(args.out_dir)

    if args.print_sources and not all(
            os.path.exists(gn_target.manifest_path(build_dir))
            for gn_target in args.gn_targets):
        return 1

    cargo_tomls = []  # List of (gn_target, cargo_toml)
    for gn_target in args.gn_targets:
        with open(gn_target.manifest_path(build_dir), "r") as fin:
            cargo_tomls.append((gn_target, toml.load(fin)))

    main_files = []  # List of (gn_target, main_file)
    for gn_target, cargo_toml in cargo_tomls:
        if 'bin' in cargo_toml:
            bins = cargo_toml['bin']
            if len(bins) != 1:
                print(f"Expected a single bin target for {gn_target}, found {len(bins)}")
                return 1
            main_files.append((gn_target, bins[0]['path']))
        elif 'lib' in cargo_toml:
            main_files.append((gn_target, cargo_toml['lib']['path']))

    if args.print_sources:
        if main_files:
            print('\n'.join(main_file[1] for main_file in main_files))
        return 0

    for gn_target, main_file in main_files:
        if not main_file or not os.path.exists(main_file):
            print(f"No source root (typically lib.rs or main.rs) found for {gn_target}")
            return 1

    host_platform = "%s-%s" % (
        platform.system().lower().replace("darwin", "mac"),
        {
            "x86_64": "x64",
            "aarch64": "arm64",
        }[platform.machine()],
    )
    buildtools_dir = os.path.join(ROOT_PATH, "prebuilt", "third_party")
    rustfmt = os.path.join(
        buildtools_dir, "rust_tools", host_platform, "bin", "rustfmt")

    call_args = [rustfmt] + [main_file[1] for main_file in main_files]

    if args.verbose:
        call_args.append("-v")

    return subprocess.call(call_args)


if __name__ == '__main__':
    sys.exit(main())
