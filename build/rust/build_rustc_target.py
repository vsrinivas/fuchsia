#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import subprocess
import sys

TERM_COLOR_RED = '\033[91m'
TERM_COLOR_END = '\033[0m'

# Updates the path of the main target in the depfile to the relative path
# from base_path build_output_path
def fix_depfile(depfile_path, base_path, build_output_path):
    with open(depfile_path, "r") as depfile:
        content = depfile.read()
    content_split = content.split(': ', 1)
    target_path = os.path.relpath(build_output_path, start=base_path)
    new_content = "%s: %s" % (target_path, content_split[1])
    with open(depfile_path, "w") as depfile:
        depfile.write(new_content)

# Creates the directory containing the given file.
def create_base_directory(file):
    path = os.path.dirname(file)
    try:
        os.makedirs(path)
    except os.error:
        # Already existed.
        pass

# Starts the given command and returns the newly created job.
def start_command(args, env):
    return subprocess.Popen(args, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)

def main():
    parser = argparse.ArgumentParser("Compiles a Rust crate")
    parser.add_argument("--rustc",
                        help="Path to rustc",
                        required=True)
    parser.add_argument("--cipd-version",
                        help="CIPD version of Rust toolchain",
                        required=False)
    parser.add_argument("--crate-root",
                        help="Path to source directory",
                        required=True)
    parser.add_argument("--cargo-toml-dir",
                        help="Path to directory in which a Cargo.toml for this target may be generated",
                        required=True)
    parser.add_argument("--crate-type",
                        help="Type of crate to build",
                        required=True,
                        choices=["bin", "rlib", "staticlib", "proc-macro"])
    parser.add_argument("--package-name",
                        help="Name of package to build",
                        required=True)
    parser.add_argument("--crate-name",
                        help="Name of crate to build",
                        required=True)
    parser.add_argument("--version",
                        help="Semver version of the crate being built",
                        required=True)
    parser.add_argument("--edition",
                        help="Edition of rust to use when compiling the crate",
                        required=True,
                        choices=["2015", "2018"])
    parser.add_argument("--opt-level",
                        help="Optimization level",
                        required=True,
                        choices=["0", "1", "2", "3", "s", "z"])
    parser.add_argument("--lto",
                        help="Use LTO",
                        required=False,
                        choices=["thin", "fat"])
    parser.add_argument("--output-file",
                        help="Path at which the output file should be stored",
                        required=True)
    parser.add_argument("--depfile",
                        help="Path at which the output depfile should be stored",
                        required=True)
    parser.add_argument("--root-out-dir",
                        help="Root output dir on which depfile paths should be rebased",
                        required=True)
    parser.add_argument("--test-output-file",
                        help="Path at which the unit test output file should be stored if --with-unit-tests is supplied",
                        required=False)
    parser.add_argument("--with-unit-tests",
                        help="Whether or not to build unit tests",
                        action="store_true",
                        required=False)
    parser.add_argument("--target",
                        help="Target for which this crate is being compiled",
                        required=True)
    parser.add_argument("--cmake-dir",
                        help="Path to the directory containing cmake",
                        required=True)
    parser.add_argument("--clang_prefix",
                        help="Path to the clang prefix",
                        required=True)
    parser.add_argument("--clang-resource-dir",
                        help="Path to the clang resource dir",
                        required=True)
    parser.add_argument("--sysroot",
                        help="Path to the sysroot",
                        required=True)
    parser.add_argument("--lib-dir",
                        help="Link path for binary libraries",
                        action='append', default=[])
    parser.add_argument("--lib-dir-file",
                        help="File of --lib-dir directory names, one per line")
    parser.add_argument("--first-party-crate-root",
                        help="Path to directory containing the libs for first-party dependencies",
                        required=True)
    parser.add_argument("--third-party-deps-data",
                        help="Path to output of third_party_crates.py",
                        required=True)
    parser.add_argument("--out-info",
                        help="Path metadata output",
                        required=True)
    parser.add_argument("--dep-data",
                        action="append",
                        help="Path to metadata from a crate dependency",
                        required=False)
    parser.add_argument("--mmacosx-version-min",
                        help="Select macosx framework version",
                        required=False)
    parser.add_argument("--symbol-level",
                        help="Symbols to include (0=none, 1=minimal, 2=full)",
                        choices=["0", "1", "2"],
                        required=True)
    parser.add_argument("--warnings",
                        help="Whether or not to error on warnings (deny=error, allow=ignore)",
                        choices=["deny", "allow"],
                        required=True)
    parser.add_argument("--unstable-rust-feature",
                        help="Unstable Rust feature to allow",
                        action="append",
                        dest="unstable_rust_features",
                        required=False)
    parser.add_argument("--feature",
                        help="Feature to enable",
                        action="append",
                        dest="features",
                        required=False)
    parser.add_argument("--remap-path-prefix",
                        help="Remap source names in output",
                        action="append",
                        required=False)

    parser.add_argument
    args = parser.parse_args()

    env = os.environ.copy()
    env["CC"] = os.path.join(args.clang_prefix, "clang")
    env["CXX"] = os.path.join(args.clang_prefix, "clang++")
    env["AR"] = os.path.join(args.clang_prefix, "llvm-ar")
    env["RANLIB"] = os.path.join(args.clang_prefix, "llvm-ranlib")
    if args.cmake_dir:
        env["PATH"] = "%s:%s" % (env["PATH"], args.cmake_dir)
    env["RUST_BACKTRACE"] = "1"

    create_base_directory(args.output_file)

    if args.warnings == "allow":
        warnings_flag = "-Awarnings"
    else:
        warnings_flag = "-Dwarnings"

    if args.lib_dir_file:
        with open(args.lib_dir_file) as f:
            args.lib_dir += [line.strip() for line in f.readlines()]

    call_args = [
        args.rustc,
        args.crate_root,
        warnings_flag,
        "--edition=%s" % args.edition,
        "--crate-type=%s" % args.crate_type,
        "--crate-name=%s" % args.crate_name,
        "--target=%s" % args.target,
        "-Copt-level=%s" % args.opt_level,
        "-Cdebuginfo=%s" % args.symbol_level,
        "--color=always",
        "-Zallow-features=%s" % ",".join(args.unstable_rust_features or [])
    ]
    call_args += ["-Lnative=%s" % dir for dir in args.lib_dir]
    if args.features:
        for feature in args.features:
            call_args += ["--cfg", "feature=\"%s\"" % feature]
    if args.remap_path_prefix:
        for path_prefix in args.remap_path_prefix:
            call_args += ["--remap-path-prefix", path_prefix]

    if args.target.endswith("fuchsia"):
        call_args += [
            "-L", os.path.join(args.sysroot, "lib"),
            "-Clink-arg=--pack-dyn-relocs=relr",
            "-Clink-arg=--sysroot=%s" % args.sysroot,
            "-Clink-arg=-L%s" % os.path.join(args.sysroot, "lib"),
            "-Clink-arg=-L%s" % os.path.join(args.clang_resource_dir, args.target, "lib"),
            "-Clink-arg=--threads",
            "-Clink-arg=-dynamic-linker=ld.so.1",
        ]
        if args.target.startswith("aarch64"):
            call_args += ["-Clink-arg=--fix-cortex-a53-843419"]
    else:
        call_args += [
            "-Clinker=%s" % os.path.join(args.clang_prefix, "clang"),
        ]
        if args.target.startswith("aarch64"):
            call_args += ["-Clink-arg=-Wl,--fix-cortex-a53-843419"]
        if args.target.endswith("linux-gnu"):
            call_args += ["-Clink-arg=-Wl,--build-id"]
        if not args.target.endswith("darwin"):
            call_args += ["-Clink-arg=-Wl,--threads"]

    if args.mmacosx_version_min:
        call_args += [
            "-Clink-arg=-mmacosx-version-min=%s" % args.mmacosx_version_min,
        ]

    if args.lto:
        call_args += ["-Clto=%s" % args.lto]

    third_party_json = json.load(open(args.third_party_deps_data))
    search_paths = third_party_json["deps_folders"] + [ args.first_party_crate_root ]
    for path in search_paths:
        call_args += ["-L", "dependency=%s" % path]

    externs = []

    # Collect externs
    if args.dep_data:
        for data_path in args.dep_data:
            if not os.path.isfile(data_path):
                print TERM_COLOR_RED
                print "Missing Rust target data for dependency " + data_path
                print "Did you accidentally depend on a non-Rust target?"
                print TERM_COLOR_END
                return -1
            dep_data = json.load(open(data_path))
            if dep_data["third_party"]:
                package_name = dep_data["package_name"]
                if package_name not in third_party_json["crates"]:
                    print(TERM_COLOR_RED)
                    print("Missing Rust target dependency: " + data_path)
                    print("Package is present in the third_party/ but is absent"
                          " from dependency data: " + args.third_party_deps_data)
                    print("Maybe this package is conditionally disabled for the"
                          " current configuration of '%s'?" % args.crate_name)
                    print(TERM_COLOR_END)
                    return -1
                crate_data = third_party_json["crates"][package_name]
                crate = crate_data["crate_name"]
                lib_path = crate_data["lib_path"]
            else:
                crate = dep_data["crate_name"]
                lib_path = dep_data["lib_path"]
            crate_underscore = crate.replace("-", "_")
            externs.append("%s=%s" % (crate_underscore, lib_path))

    # add externs to arguments
    for extern in externs:
        call_args += ["--extern", extern]

    # Build the depfile
    depfile_args = call_args + [
        "-o%s" % args.depfile,
        "--emit=dep-info",
    ]
    if args.with_unit_tests:
        depfile_args += ["--test"]
    depfile_job = start_command(depfile_args, env)

    # Build the desired output
    build_args = call_args + ["-o%s" % args.output_file]
    build_job = start_command(build_args, env)

    # Build the test harness
    if args.with_unit_tests:
        build_test_args = call_args + [
            "-o%s" % args.test_output_file,
            "--test",
        ]
        test_job = start_command(build_test_args, env)

    # Write output dependency info
    create_base_directory(args.out_info)
    with open(args.out_info, "w") as file:
        file.write(json.dumps({
            "crate_name": args.crate_name,
            "package_name": args.package_name,
            "third_party": False,
            "cargo_toml_dir": args.cargo_toml_dir,
            "lib_path": args.output_file,
            "version": args.version,
        }, sort_keys=True, indent=4, separators=(",", ": ")))

    # Wait for build jobs to complete

    stdout, stderr = depfile_job.communicate()
    if depfile_job.returncode != 0:
        print(stdout + stderr)
        return depfile_job.returncode
    fix_depfile(args.depfile, os.getcwd(), args.output_file)

    stdout, stderr = build_job.communicate()
    if build_job.returncode != 0:
        print(stdout + stderr)
        return build_job.returncode

    if args.with_unit_tests:
        stdout, stderr = test_job.communicate()
        if test_job.returncode != 0:
            print(stdout + stderr)
            return test_job.returncode

if __name__ == '__main__':
    sys.exit(main())
