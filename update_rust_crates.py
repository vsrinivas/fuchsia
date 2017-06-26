#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import paths
import platform
import shutil
import subprocess
import sys
import tempfile

sys.path += [os.path.join(paths.FUCHSIA_ROOT, "third_party", "pytoml")]
import pytoml as toml

from check_rust_licenses import check_licenses


CONFIGS = [
    "apps/xi/modules/xi-core",
    "lib/fidl/rust/fidl",
    "rust/magenta-rs",
    "rust/magenta-rs/magenta-sys",
    "rust/rust_sample_module",
]


def get_cargo_bin():
    host_os = platform.system()
    if host_os == "Darwin":
        host_triple = "x86_64-apple-darwin"
    elif host_os == "Linux":
        host_triple = "x86_64-unknown-linux-gnu"
    else:
        raise Exception("Platform not supported: %s" % host_os)
    return os.path.join(paths.FUCHSIA_ROOT, "buildtools", "rust",
                        "rust-%s" % host_triple, "bin", "cargo")


def list_required_native_libraries(vendor_dir):
    result = []
    for item in os.listdir(vendor_dir):
        if not os.path.isdir(item):
            continue
        with open(os.path.join(item, "Cargo.toml"), "r") as cargo_file:
            config = toml.load(cargo_file)
            if "links" in config["package"]:
                result.append(config["package"]["links"])
    return result


def list_known_native_libraries(crates_dir):
    globals = {}
    execfile(os.path.join(crates_dir, "libraries.gni"), globals)
    return globals["rust_native_libs_names"]


def call_or_exit(args, dir):
    if subprocess.call(args, cwd=dir) != 0:
        raise Exception("Command failed in %s: %s" % (dir, " ".join(args)))


def main():
    parser = argparse.ArgumentParser("Updates third-party Rust crates")
    parser.add_argument("--cargo-vendor",
                        help="Path to the cargo-vendor command",
                        required=True)
    parser.add_argument("--debug",
                        help="Debug mode",
                        action="store_true")
    args = parser.parse_args()

    # Use the root of the tree as the working directory. Ideally a temporary
    # directory would be used, but unfortunately this would break the flow as
    # the configs used to seed the vendor directory must be under a common
    # parent directory.
    base_dir = paths.FUCHSIA_ROOT

    toml_path = os.path.join(base_dir, "Cargo.toml")
    lock_path = os.path.join(base_dir, "Cargo.lock")

    try:
        print("Downloading dependencies for:")
        for config in CONFIGS:
            print(" - %s" % config)

        # Create Cargo.toml.
        def mapper(p): return os.path.join(paths.FUCHSIA_ROOT, p)
        config = {
            "workspace": {
                "members": list(map(mapper, CONFIGS))
            }
        }
        with open(toml_path, "w") as config_file:
            toml.dump(config, config_file)

        cargo_bin = get_cargo_bin()

        # Generate Cargo.lock.
        lockfile_args = [
            cargo_bin,
            "generate-lockfile",
        ]
        call_or_exit(lockfile_args, base_dir)

        # Populate the vendor directory.
        vendor_args = [
            args.cargo_vendor,
            "-x",
            "--sync",
            lock_path,
            "vendor",
        ]
        call_or_exit(vendor_args, base_dir)
    finally:
        if not args.debug:
            os.remove(toml_path)
            os.remove(lock_path)

    crates_dir = os.path.join(paths.FUCHSIA_ROOT, "third_party", "rust-crates")
    vendor_dir = os.path.join(crates_dir, "vendor")
    shutil.rmtree(vendor_dir)
    shutil.move(os.path.join(paths.FUCHSIA_ROOT, "vendor"), vendor_dir)

    print("Verifying licenses...")
    if not check_licenses(vendor_dir):
        print("Some licenses are missing!")
        return 1

    print("Verifying native libraries...")
    required_libraries = list_required_native_libraries(vendor_dir)
    known_libraries = list_known_native_libraries(crates_dir)
    if set(required_libraries) != set(known_libraries):
        print("Native libraries in libraries.gni need to be updated:")
        for lib in required_libraries:
            print(" - %s" % lib)
        return 1

    update_file = os.path.join(crates_dir, ".vendor-update.stamp")
    # Write the timestamp file.
    # This file is necessary in order to trigger rebuilds of Rust artifacts
    # whenever third-party dependencies are updated.
    with open(update_file, "a"):
        os.utime(update_file, None)

    print("Vendor directory updated at %s" % vendor_dir)


if __name__ == '__main__':
    sys.exit(main())
