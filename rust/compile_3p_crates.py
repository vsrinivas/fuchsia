#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import subprocess
import sys

ROOT_PATH = os.path.abspath(__file__ + "/../../..")
sys.path += [os.path.join(ROOT_PATH, "third_party", "pytoml")]
import pytoml

# "foo" from "foo 0.1.0 (//path/to/crate)"
def package_name_from_crate_id(crate_id):
    return crate_id.split(" ")[0]

# Removes the (//path/to/crate) from the crate id "foo 0.1.0 (//path/to/crate)"
# This is necessary in order to support locally-patched (mirrored) crates
def pathless_crate_id(crate_id):
    split_id = crate_id.split(" ")
    return split_id[0] + " " + split_id[1]

# Creates the directory containing the given file.
def create_base_directory(file):
    path = os.path.dirname(file)
    try:
        os.makedirs(path)
    except os.error:
        # Already existed.
        pass

# Runs the given command and returns its return code and output.
def run_command(args, env, cwd):
    job = subprocess.Popen(args, env=env, cwd=cwd, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
    stdout, stderr = job.communicate()
    return (job.returncode, stdout, stderr)

def main():
    parser = argparse.ArgumentParser("Compiles all third-party Rust crates")
    parser.add_argument("--rustc",
                        help="Path to rustc",
                        required=True)
    parser.add_argument("--cargo",
                        help="Path to the cargo tool",
                        required=True)
    parser.add_argument("--crate-root",
                        help="Path to the crate root",
                        required=True)
    parser.add_argument("--opt-level",
                        help="Optimization level",
                        required=True,
                        choices=["0", "1", "2", "3", "s", "z"])
    parser.add_argument("--out-dir",
                        help="Path at which the output libraries should be stored",
                        required=True)
    parser.add_argument("--out-deps-data",
                        help="File in which data about the dependencies should be stored",
                        required=True)
    parser.add_argument("--target",
                        help="Target for which this crate is being compiled",
                        required=True)
    parser.add_argument("--cmake-dir",
                        help="Path to the directory containing cmake",
                        required=True)
    parser.add_argument("--clang_prefix",
                        help="Path to the clang prefix",
                        required=True)
    # TODO(cramertj) make these args required when the third_party/rust-crates change lands
    parser.add_argument("--sysroot",
                        help="Path to the sysroot",
                        required=False)
    parser.add_argument("--shared-libs-root",
                        help="Path to the location of shared libraries",
                        required=False)
    parser.add_argument
    args = parser.parse_args()

    env = os.environ.copy()
    create_base_directory(args.out_dir)

    clang_c_compiler = os.path.join(args.clang_prefix, "clang")

    env["CARGO_TARGET_LINKER"] = clang_c_compiler
    env["CARGO_TARGET_X86_64_APPLE_DARWIN_LINKER"] = clang_c_compiler
    env["CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER"] = clang_c_compiler
    env["CARGO_TARGET_%s_LINKER" % args.target.replace("-", "_").upper()] = clang_c_compiler
    if args.sysroot and args.shared_libs_root:
        env["CARGO_TARGET_%s_RUSTFLAGS" % args.target.replace("-", "_").upper()] = (
            "-Clink-arg=--target=" + args.target +
            " -Copt-level=" + args.opt_level +
            " -Clink-arg=--sysroot=" + args.sysroot +
            " -Lnative=" + args.shared_libs_root
        )
    else:
        env["CARGO_TARGET_%s_RUSTFLAGS" % args.target.replace("-", "_").upper()] = (
            "-Clink-arg=--target=" + args.target +
            " -Copt-level=" + args.opt_level
        )
    env["CARGO_TARGET_DIR"] = args.out_dir
    env["CARGO_BUILD_DEP_INFO_BASEDIR"] = args.out_dir
    env["RUSTC"] = args.rustc
    env["RUST_BACKTRACE"] = "1"
    env["CC"] = clang_c_compiler
    if args.sysroot and args.shared_libs_root:
        env["CFLAGS"] = "--sysroot=%s -L %s" % (args.sysroot, args.shared_libs_root)
    env["CXX"] = os.path.join(args.clang_prefix, "clang++")
    env["AR"] = os.path.join(args.clang_prefix, "llvm-ar")
    env["RANLIB"] = os.path.join(args.clang_prefix, "llvm-ranlib")
    env["PATH"] = "%s:%s" % (env["PATH"], args.cmake_dir)

    call_args = [
        args.cargo,
        "build",
        "--color=always",
        "--target=%s" % args.target,
        "--frozen",
    ]

    call_args.append("--message-format=json")

    retcode, stdout, stderr = run_command(call_args, env, args.crate_root)
    if retcode != 0:
        # The output is not particularly useful as it is formatted in JSON.
        # Re-run the command with a user-friendly format instead.
        del call_args[-1]
        _, stdout, stderr = run_command(call_args, env, args.crate_root)
        print(stdout + stderr)
        return retcode

    cargo_toml_path = os.path.join(args.crate_root, "Cargo.toml")
    with open(cargo_toml_path, "r") as file:
        cargo_toml = pytoml.load(file)

    crate_id_to_info = {}
    deps_folders = set()
    for line in stdout.splitlines():
        data = json.loads(line)
        if "filenames" not in data:
            continue
        crate_id = pathless_crate_id(data["package_id"])
        assert len(data["filenames"]) == 1
        lib_path = data["filenames"][0]
        crate_name = data["target"]["name"]
        if crate_name != "fuchsia-third-party":
            # go from e.g. target/debug/deps/libfoo.rlib to target/debug/deps
            deps_folders.add(os.path.dirname(lib_path))

        crate_id_to_info[crate_id] = {
            "crate_name": crate_name,
            "lib_path": lib_path,
        }

    cargo_lock_path = os.path.join(args.crate_root, "Cargo.lock")
    with open(cargo_lock_path, "r") as file:
        cargo_lock_toml = pytoml.load(file)

    # output the info for fuchsia-third-party's direct dependencies
    crates = {}
    for package in cargo_lock_toml["package"]:
        if package["name"] == "fuchsia-third-party":
            for crate_id in package["dependencies"]:
                crate_id = pathless_crate_id(crate_id)
                crate_info = crate_id_to_info[crate_id]
                crate_name = crate_info["crate_name"]
                package_name = package_name_from_crate_id(crate_id)

                if not package_name in cargo_toml["dependencies"]:
                    print (package_name + " not found in Cargo.toml dependencies section")
                    return 1
                crate_info["cargo_dependency_toml"] = cargo_toml["dependencies"][package_name]

                crates[package_name] = crate_info

    # normalize paths for patches
    patches = cargo_toml["patch"]["crates-io"]
    for patch in patches:
        path = patches[patch]["path"]
        path = os.path.join(args.crate_root, path)
        patches[patch] = { "path": path }

    create_base_directory(args.out_deps_data)
    with open(args.out_deps_data, "w") as file:
        file.write(json.dumps({
            "crates": crates,
            "deps_folders": list(deps_folders),
            "patches": patches,
        }, sort_keys=True, indent=4, separators=(",", ": "))) # for humans

if __name__ == '__main__':
    sys.exit(main())
