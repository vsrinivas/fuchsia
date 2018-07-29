#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import subprocess
import sys

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

# Runs the given command and returns its return code and output.
def run_command(args, env):
    job = subprocess.Popen(args, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
    stdout, stderr = job.communicate()
    return (job.returncode, stdout, stderr)

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
    parser.add_argument("--sysroot",
                        help="Path to the sysroot",
                        required=True)
    parser.add_argument("--shared-libs-root",
                        help="Path to the location of shared libraries",
                        required=True)
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

    call_args = [
        args.rustc,
        args.crate_root,
        "--edition=%s" % args.edition,
        "--crate-type=%s" % args.crate_type,
        "--crate-name=%s" % args.crate_name,
        "--target=%s" % args.target,
        "-Clinker=%s" % os.path.join(args.clang_prefix, "clang"),
        "-Clink-arg=--target=%s" % args.target,
        "-Clink-arg=--sysroot=%s" % args.sysroot,
        "-Copt-level=%s" % args.opt_level,
        "-Lnative=%s" % args.shared_libs_root,
        "--color=always",
    ]
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
            dep_data = json.load(open(data_path))
            if dep_data["third_party"]:
                package_name = dep_data["package_name"]
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
    retcode, stdout, stderr = run_command(depfile_args, env)
    if retcode != 0:
        print(stdout + stderr)
        return retcode
    fix_depfile(args.depfile, os.getcwd(), args.output_file)

    # Build the desired output
    build_args = call_args + ["-o%s" % args.output_file]
    retcode, stdout, stderr = run_command(build_args, env)
    if retcode != 0:
        print(stdout + stderr)
        return retcode

    # Build the test harness
    if args.with_unit_tests:
        build_test_args = call_args + [
            "-o%s" % args.test_output_file,
            "--test",
        ]
        retcode, stdout, stderr = run_command(build_test_args, env)
        if retcode != 0:
            print(stdout + stderr)
            return retcode

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

if __name__ == '__main__':
    sys.exit(main())
