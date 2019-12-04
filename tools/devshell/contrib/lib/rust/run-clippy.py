#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### Runs `cargo clippy` on a target or on a set of files.

import argparse
import json
import os
import subprocess
import sys

from contextlib import contextmanager

import rust
from rust import FUCHSIA_BUILD_DIR, HOST_PLATFORM, PREBUILT_THIRD_PARTY_DIR, \
    get_rust_target_from_file

PATH = os.environ["PATH"]
THIRD_PARTY_DEPS_DATA = os.path.join(
    FUCHSIA_BUILD_DIR, 'rust_third_party_crates', 'deps_data.json')
HOST_THIRD_PARTY_DEPS_DATA = os.path.join(
    FUCHSIA_BUILD_DIR, 'host_x64', 'rust_third_party_crates', 'deps_data.json')

# Cargo args to ignore from the deps_data.json files.
CARGO_IGNORE_ARGS = ['--frozen', '--locked']

@contextmanager
def cwd(dir):
    previous = os.getcwd()
    os.chdir(dir)
    try:
        yield
    finally:
        os.chdir(previous)


def main():
    parser = argparse.ArgumentParser("Run cargo clippy on a file or target.")
    parser.add_argument(
        "--target",
        help="GN target on which to run clippy",
        default=None)
    parser.add_argument(
        "--file",
        dest="files",
        help="file on which to run clippy",
        action="append",
        default=[])
    parser.add_argument(
        "-v", help="verbose", action="store_true", default=False)
    args = parser.parse_args()

    targets = set()
    if args.target:
        try:
            targets.add(rust.GnTarget(target))
        except ValueError as e:
            # The target may not be a Rust one, so it's okay if it didn't find the Cargo.toml.
            if args.v:
                print "No Rust target found for %s: %s" % (target, e)

    for file in args.files:
        # Skip non-rust files.
        if not file.endswith(".rs"):
            continue

        try:
            target = get_rust_target_from_file(file)
            if not target:
                return 1
        except ValueError as e:
            # The target should be a Rust one, so any error is bad.
            print "No Rust target found for %s: %s" % (file, e)
            return 1

        targets.add(target)

    cargos = set()
    for target in targets:
        cargo = target.manifest_path(FUCHSIA_BUILD_DIR)
        if cargo and os.path.isfile(cargo):
            cargos.add(cargo)
        else:
            print "Cargo.toml file not found for %s, try running fx build." % target
            return 1

    rust_extra_prebuilts = \
        os.path.join(PREBUILT_THIRD_PARTY_DIR, "rust_extra_tools", HOST_PLATFORM, "bin")
    rust_prebuilts = os.path.join(PREBUILT_THIRD_PARTY_DIR, "rust", HOST_PLATFORM, "bin")

    # The third_party build records the arguments it used to invoke cargo. Use the same ones for
    # the clippy invocation.
    third_party_deps_data = json.load(open(THIRD_PARTY_DEPS_DATA, 'r'))
    host_third_party_deps_data = json.load(
        open(HOST_THIRD_PARTY_DEPS_DATA, 'r'))

    env = {}
    env['PATH'] = ":".join([rust_extra_prebuilts, rust_prebuilts, PATH])
    call_args = [
        "cargo",
        "clippy",
    ]

    if args.v:
        call_args.append("-v")

    for cargo in cargos:
        if 'host_x64' in cargo:
            cargo_args = host_third_party_deps_data['cargo_args']
        else:
            cargo_args = third_party_deps_data['cargo_args']
        cargo_args = filter(lambda arg: arg not in CARGO_IGNORE_ARGS, cargo_args)
        # Some crates use #![deny(warnings)], which will cause clippy to fail entirely if it finds
        # issues in those crates. Cap all lints at `warn` to avoid this.
        cargo_args += ['--', '--cap-lints', 'warn']
        if args.v:
            print " ".join(call_args + cargo_args)
        with cwd(os.path.dirname(cargo)):
            return subprocess.call(call_args + cargo_args, env=env)

    return 0

if __name__ == '__main__':
    sys.exit(main())
