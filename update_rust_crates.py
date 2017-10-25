#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import fileinput
import os
import paths
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import uuid

sys.path += [os.path.join(paths.FUCHSIA_ROOT, "third_party", "pytoml")]
import pytoml
sys.path += [os.path.join(paths.FUCHSIA_ROOT, "build", "rust")]
import local_crates

from check_rust_licenses import check_licenses

# The following crates are ignored and their dependencies will not be accounted during vendoring.
EXCLUDED_CRATES = [
    "topaz/app/xi/modules/xi-core",
    # llui is disabled because: a) it uses a non-path dependency on fuchsia-zircon, b) it uses a git dependency
    "garnet/public/rust/crates/fuchsia-llui",
    # XXX(raggi): rand is disabled from consideration here because it contains a workspace.
    "third_party/rust-mirrors/rand",
    "third_party/rust-mirrors/rand/rand-derive",
    # XXX(raggi): cargo-vendor is excluded because it has a large dependency tree that isn't used/considered at build time.
    "third_party/rust-mirrors/cargo-vendor",
]

NATIVE_LIBS = {
    # miniz is built inline. the links declaration linsk a lib output by the build.rs. nothing to depend on.
    "miniz": None,
}


def get_cargo_bin():
    host_os = platform.system()
    if host_os == "Darwin":
        platform_dir = "mac-x64"
    elif host_os == "Linux":
        platform_dir = "linux-x64"
    else:
        raise Exception("Platform not supported: %s" % host_os)
    return os.path.join(paths.FUCHSIA_ROOT, "buildtools", platform_dir, "rust",
                        "bin", "cargo")


def parse_dependencies(lock_path):
    """Extracts the crate dependency tree from a lockfile."""
    result = []
    with open(lock_path, "r") as lock_file:
        content = pytoml.load(lock_file)
        dep_matcher = re.compile("^([^\s]+)\s([^\s]+)")
        crates_source = "registry+https://github.com/rust-lang/crates.io-index"
        for package in content["package"]:
            name = package["name"]
            if "source" in package and package["source"].startswith("git"):
                raise Exception("Found git dependency on %s, "
                                "use an explicit version instead." % name)
            from_crates_io = ("source" in package and
                              package["source"] == crates_source)
            deps = []
            if "dependencies" in package:
                for dep in package["dependencies"]:
                    match = dep_matcher.match(dep)
                    if match:
                        deps.append("%s-%s" % (match.group(1), match.group(2)))
            label = "%s-%s" % (package["name"], package["version"])
            result.append({
                "name": name,
                "version": package["version"],
                "label": label,
                "deps": deps,
                "from_crates_io": from_crates_io,
            })
    return result


def update_crates(crates):
    """Adjusts the list of crates and adds more data."""
    print("Updating crates data...")
    result = []

    # Account for local crates.
    for crate in crates:
        name = crate["name"]
        version = crate["version"]
        is_mirror = name in local_crates.RUST_CRATES["mirrors"]
        is_from_crates_io = crate["from_crates_io"]

        # Never generate build rules for Fuchsia crates.
        if name in local_crates.RUST_CRATES["published"]:
            published = local_crates.RUST_CRATES["published"][name]
            if published["version"] == version:
                print("Ignoring published crate '%s'" % crate["label"])
                continue

        if is_mirror:
            if not is_from_crates_io:
                # A build rule is needed for this crate.
                print("Generating build rule for mirror '%s'" % name)
                crate["path"] = os.path.join(paths.FUCHSIA_ROOT, "third_party",
                                             "rust-mirrors", name)
                result.append(crate)
            continue

        if not is_from_crates_io:
            print("Ignoring local crate '%s'" % name)
            continue

        crate["path"] = os.path.join(paths.FUCHSIA_ROOT, "third_party",
                                     "rust-crates", "vendor", crate["label"])
        result.append(crate)

    # Map deps to GN deps.
    source_crates = dict(map(lambda (k, v): ("%s-%s" % (k, v["version"]),
                                             v["target"]),
                             local_crates.RUST_CRATES["published"].iteritems()))
    for crate in result:
        deps = []
        for dep in crate["deps"]:
            if dep in source_crates:
                dep_target = source_crates[dep]
            else:
                dep_target = ":%s" % dep
            deps.append(dep_target)
        crate["deps"] = deps
    return result


def add_native_libraries(crates, vendor_dir):
    """Returns true if all native libraries could be identified and added to
       the given crate metadata."""
    result = True
    for crate in crates:
        config_path = os.path.join(crate["path"], "Cargo.toml")
        with open(config_path, "r") as config_file:
            config = pytoml.load(config_file)
            if "links" in config["package"]:
                library = config["package"]["links"]
                if library not in NATIVE_LIBS:
                    print("Unknown native library: %s" % library)
                    result = False
                    continue
                crate["native_lib"] = library
    return result


def generate_build_file(build_path, crates):
    """Creates a BUILD.gn file for the given crates."""
    crates.sort(key=lambda c: c["label"])
    with open(build_path, "w") as build_file:
        build_file.write("""# Generated by //scripts/update_rust_crates.py.

import("//build/rust/rust_info.gni")
""")
        for info in crates:
            build_file.write("""
rust_info("%s") {
  name = "%s"
"""
 % (info["label"], info["name"]))
            if info["deps"]:
                build_file.write("\n  deps = [\n")
                for dep in info["deps"]:
                    build_file.write("    \"%s\",\n" % dep)
                build_file.write("  ]\n")
            if "native_lib" in info:
                lib = info["native_lib"]
                if NATIVE_LIBS[lib] is not None:
                    build_file.write("""
  native_lib = \"%s\"

  non_rust_deps = [
    \"%s\",
  ]
""" % (lib, NATIVE_LIBS[lib]))
            build_file.write("}\n")


def fix_build_files(crates):
    """Updates BUILD.gn files with newer versions of third-party crates."""
    dep_pattern = re.compile(
            r"^(\s*)\"//third_party/rust-crates:([a-z\-]+)-(\d[\d\.]+)\",\s*$")
    not_found = set()
    for root, dirs, files in os.walk(os.path.join(paths.FUCHSIA_ROOT)):
        for file in files:
            _, ext = os.path.splitext(file)
            if file != "BUILD.gn" and ext != ".gni":
                continue
            base = os.path.relpath(root, paths.FUCHSIA_ROOT)
            if base == "third_party/rust-crates":
                # The build file defining the crates is up-to-date, thank you
                # very much.
                continue
            path = os.path.join(root, file)
            for line in fileinput.input(path, inplace=1):
                match = dep_pattern.match(line)
                if not match:
                    sys.stdout.write(line)
                    continue
                crate_name = match.group(2)
                new_version = next((x["version"] for x in crates
                                    if x["name"] == crate_name), None)
                if not new_version:
                    sys.stdout.write(line)
                    not_found.add(crate_name)
                    continue
                sys.stdout.write("%s\"//third_party/rust-crates:%s-%s\",\n" %
                                 (match.group(1), crate_name, new_version))
    if not_found:
        print("Unable to find new versions for:")
        for crate in not_found:
            print("  - %s" % crate)
        return False
    return True


def call_or_exit(args, dir):
    if subprocess.call(args, cwd=dir) != 0:
        raise Exception("Command failed in %s: %s" % (dir, " ".join(args)))


def main():
    parser = argparse.ArgumentParser("Updates third-party Rust crates")
    parser.add_argument("--cargo-vendor",
                        help="Path to the cargo-vendor command",
                        default=os.path.join(paths.FUCHSIA_ROOT, "out",
                                             "cargo-vendor", "debug",
                                             "cargo-vendor"))
    parser.add_argument("--debug",
                        help="Debug mode",
                        action="store_true")
    args = parser.parse_args()

    if not os.path.isfile(args.cargo_vendor):
        print("!!! No cargo-vendor binary at %s !!!" % args.cargo_vendor)
        print("You might need to run //scripts/build_cargo_vendor.sh first.")
        return 1

    # Use the root of the tree as the working directory. Ideally a temporary
    # directory would be used, but unfortunately this would break the flow as
    # the configs used to seed the vendor directory must be under a common
    # parent directory.
    base_dir = paths.FUCHSIA_ROOT

    toml_path = os.path.join(base_dir, "Cargo.toml")
    lock_path = os.path.join(base_dir, "Cargo.lock")

    all_configs = local_crates.get_really_all_paths()
    for path in EXCLUDED_CRATES:
        all_configs.remove(path)


    try:
        print("Downloading dependencies for:")
        for config in all_configs:
            print(" - %s" % config)

        config = {
            "workspace": {
                "members": list(all_configs)
            }
        }
        with open(toml_path, "w") as config_file:
            pytoml.dump(config, config_file)

        cargo_bin = get_cargo_bin()

        # Generate Cargo.lock.
        lockfile_args = [
            cargo_bin,
            "generate-lockfile",
        ]
        call_or_exit(lockfile_args, base_dir)

        crates = parse_dependencies(lock_path)

        # Populate the vendor directory.
        vendor_args = [
            args.cargo_vendor,
            "-x",
            "--sync",
            lock_path,
            "--frozen",
            "--locked",
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

    crates = update_crates(crates)

    if not add_native_libraries(crates, vendor_dir):
        print("Unable to identify all required native libraries.")
        return 1

    build_path = os.path.join(crates_dir, "BUILD.gn")
    generate_build_file(build_path, crates)

    print("Verifying licenses...")
    if not check_licenses(vendor_dir):
        print("Some licenses are missing!")
        return 1

    update_path = os.path.join(crates_dir, ".vendor-update.stamp")
    # Write the timestamp file.
    # This file is necessary in order to trigger rebuilds of Rust artifacts
    # whenever third-party dependencies are updated.
    with open(update_path, "w") as update_file:
        update_file.write("%s\n" % uuid.uuid1())

    print("Fixing build files")
    if not fix_build_files(crates):
        print("Failed to update build files")
        return 1

    print("Vendor directory updated at %s" % vendor_dir)
    return 0


if __name__ == '__main__':
    sys.exit(main())
