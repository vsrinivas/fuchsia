#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### generates symlinks to Rust Cargo.toml output files

import argparse
import os
import subprocess
import sys

import rust


def main():
    parser = argparse.ArgumentParser(
        "Generate symlinks to Rust Cargo.toml output files")
    parser.add_argument(
        "gn_target",
        type=rust.GnTarget,
        help="GN target to generate a symlink for. \
                              Use '.[:target]' to discover the cargo target \
                              for the current directory or use the \
                              absolute path to the target \
                              (relative to $FUCHSIA_DIR). \
                              For example: //garnet/bin/foo/bar:baz")
    parser.add_argument(
        "--output", help="Path to Cargo.toml to generate", required=False)
    parser.add_argument("--out-dir", help="Path to the Fuchsia build directory")
    args = parser.parse_args()

    build_dir = os.path.abspath(args.out_dir)

    if args.output:
        output = os.path.abspath(args.output)
    else:
        output = os.path.join(args.gn_target.src_path, "Cargo.toml")

    manifest_path = args.gn_target.manifest_path(build_dir=build_dir)
    if os.path.exists(manifest_path):
        try:
            os.remove(output)
        except OSError:
            pass
        print(f"Linking '{output}' for target '{args.gn_target}'")
        rel_path = os.path.relpath(manifest_path, os.path.dirname(output))
        os.symlink(rel_path, output)
    else:
        print(f"Could not find a Cargo.toml file at '{manifest_path}'")
        print("")
        print(f"Is '{args.gn_target}' an existing Rust target?")
        print("If so, make sure it is included in your build configuration and that you have run 'fx build'.")
        return 1


if __name__ == '__main__':
    sys.exit(main())
