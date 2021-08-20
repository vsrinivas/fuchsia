#!/usr/bin/env python3.8
#
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs `clippy` on a set of gn targets or rust source files

import argparse
import os
import subprocess
import sys
import time

import rust
from rust import ROOT_PATH, FUCHSIA_BUILD_DIR, HOST_PLATFORM, PREBUILT_THIRD_PARTY_DIR


def main():
    parser = argparse.ArgumentParser(
        "Run cargo clippy on a set of targets or rust files"
    )
    parser.add_argument("-v", help="verbose", action="store_true", default=False)
    parser.add_argument(
        "input",
        nargs="+",
        default=[],
    )
    parser.add_argument(
        "--files",
        action="store_true",
        help="treat the inputs as source files rather than gn targets",
    )
    args = parser.parse_args()

    if not args.input:
        parser.print_help()
        return 1

    if args.files:
        files = [os.path.abspath(f) for f in args.input]
        targets = rust.targets_from_files(files)
    else:
        targets = [rust.GnTarget(t) for t in args.input]

    os.chdir(FUCHSIA_BUILD_DIR)  # the rustflags use relpaths from the build dir
    write_config()

    prebuilt_toolchain = os.path.join(PREBUILT_THIRD_PARTY_DIR, "rust", HOST_PLATFORM)
    cargo = os.path.join(prebuilt_toolchain, "bin", "cargo")

    call_args = [
        cargo,
        "clippy",
        "--tests",
        "--target-dir=" + os.path.join(FUCHSIA_BUILD_DIR, "target"),
    ]
    if args.v:
        call_args.append("-v")
    # Some crates use #![deny(warnings)], which will cause clippy to fail entirely if it finds
    # issues in those crates. Cap all lints at `warn` to avoid this.
    clippy_args = ["--", "--cap-lints", "warn", "--no-deps"]

    env = os.environ.copy()
    env["RUSTUP_TOOLCHAIN"] = prebuilt_toolchain
    for target in targets:
        try:
            toml = get_toml_path(target)
        except ValueError as e:
            print(
                f"Cargo.toml file not found for {e}, either this isn't a rust target"
                " or you need to run `fx set` with `--cargo-toml-gen` and `fx build`"
            )
            return 1
        subprocess.run(call_args + ["--manifest-path", toml] + clippy_args, env=env)


def get_toml_path(target):
    toml = target.manifest_path(FUCHSIA_BUILD_DIR)
    if not os.path.isfile(toml):
        raise ValueError(toml)
    return toml


CONFIG_FORMAT = """
[target.x86_64-fuchsia]
rustflags = "{}"

[source.crates-io]
replace-with = "vendored-sources"

[source.vendored-sources]
directory = "../../third_party/rust_crates/vendor"

[build]
target = "x86_64-fuchsia"
"""


def write_config():
    rust_flags = " ".join(["-Cpanic=abort", "-Zpanic_abort_tests"])
    os.makedirs(".cargo", exist_ok=True)
    os.makedirs("target", exist_ok=True)
    with open(".cargo/config", "w") as f:
        f.write(CONFIG_FORMAT.format(rust_flags))


if __name__ == "__main__":
    sys.exit(main())
