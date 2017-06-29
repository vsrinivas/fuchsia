#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import build_target
import sys

"""Generates some metadata about some third-party Rust crate for consumption
   by GN. This allows GN to identify native libraries required when compiling a
   crate.
   """


def main():
    parser = argparse.ArgumentParser("Generates metadata for a 3p Rust crate")
    parser.add_argument("--name",
                        help="Name of the crate",
                        required=True)
    parser.add_argument("--label",
                        help="Label of the target",
                        required=True)
    parser.add_argument("--gen-dir",
                        help="Path to the target's generated source directory",
                        required=True)
    parser.add_argument("--root-gen-dir",
                        help="Path to the root gen directory",
                        required=True)
    parser.add_argument("--deps",
                        help="List of dependencies",
                        nargs="*")
    parser.add_argument("--native-lib",
                        help="Native library the target depends on")
    args = parser.parse_args()

    # Compute the set of native libraries required by dependencies of the
    # present crates.
    dependency_infos = build_target.gather_dependency_infos(args.root_gen_dir,
                                                            args.deps)
    native_libs = build_target.extract_native_libs(dependency_infos)

    # If the crate requires its own native library, add it to the list.
    if args.native_lib is not None and args.native_lib not in native_libs:
        native_libs.append(args.native_lib)

    # Write the metadata.
    build_target.write_target_info(args.label, args.gen_dir, args.name,
                                   native_libs,
                                   has_generated_code=False)

    return 0


if __name__ == '__main__':
    sys.exit(main())
