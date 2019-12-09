#!/usr/bin/env python2.7
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
    job = subprocess.Popen(
        args, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = job.communicate()
    return (job.returncode, stdout, stderr)


def configure_triple(triple, args, clang_c_compiler, env):
    rustflags = [
        "-Copt-level=" + args.opt_level,
        "-Cdebuginfo=" + args.symbol_level,
    ]

    if triple.endswith("fuchsia"):
        if triple.startswith("aarch64"):
            rustflags += ["-Clink-arg=--fix-cortex-a53-843419"]
        rustflags += [
            "-L",
            os.path.join(args.sysroot, "lib"),
            "-Clink-arg=--pack-dyn-relocs=relr",
            "-Clink-arg=--threads",
            "-Clink-arg=--icf=all",
            "-Clink-arg=-L%s" % os.path.join(args.sysroot, "lib"),
            "-Clink-arg=-L%s" %
            os.path.join(args.clang_resource_dir, triple, "lib"),
        ]
        if args.sysroot:
            rustflags.append("-Clink-arg=--sysroot=" + args.sysroot)
        for dir in args.lib_dir:
            rustflags.append("-Lnative=" + dir)
    else:
        if triple.startswith("aarch64"):
            rustflags += ["-Clink-arg=-Wl,--fix-cortex-a53-843419"]
        if triple.endswith("linux-gnu"):
            rustflags += ["-Clink-arg=-Wl,--build-id"]
        if not triple.endswith("darwin"):
            rustflags += [
                "-Clink-arg=-Wl,--threads", "-Clink-arg=-Wl,--icf=all"
            ]
        env["CARGO_TARGET_X86_64_APPLE_DARWIN_LINKER"] = clang_c_compiler
        env["CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER"] = clang_c_compiler
        env["CARGO_TARGET_%s_LINKER" %
            triple.replace("-", "_").upper()] = clang_c_compiler
        rustflags += ["-Clink-arg=--target=" + triple]

    if args.mmacosx_version_min:
        rustflags += [
            "-Clink-arg=-mmacosx-version-min=%s" % args.mmacosx_version_min
        ]
    env["CARGO_TARGET_%s_RUSTFLAGS" %
        triple.replace("-", "_").upper()] = (' '.join(rustflags))


def main():
    parser = argparse.ArgumentParser("Compiles all third-party Rust crates")
    parser.add_argument("--rustc", help="Path to rustc", required=True)
    parser.add_argument("--cargo", help="Path to the cargo tool", required=True)
    parser.add_argument(
        "--crate-root", help="Path to the crate root", required=True)
    parser.add_argument(
        "--opt-level",
        help="Optimization level",
        required=True,
        choices=["0", "1", "2", "3", "s", "z"])
    parser.add_argument(
        "--symbol-level",
        help="Symbols to include (0=none, 1=minimal, 2=full)",
        choices=["0", "1", "2"],
        required=True)
    parser.add_argument(
        "--out-dir",
        help="Path at which the output libraries should be stored",
        required=True)
    parser.add_argument(
        "--out-deps-data",
        help="File in which data about the dependencies should be stored",
        required=True)
    parser.add_argument(
        "--target",
        help="Target for which this crate is being compiled",
        required=True)
    parser.add_argument(
        "--host",
        help="Triple for the host which is building right now",
        required=True)
    parser.add_argument(
        "--cmake-dir",
        help="Path to the directory containing cmake",
        required=True)
    parser.add_argument(
        "--clang_prefix", help="Path to the clang prefix", required=True)
    parser.add_argument(
        "--clang-resource-dir",
        help="Path to the clang resource dir",
        required=True)
    parser.add_argument(
        "--mmacosx-version-min",
        help="Select macosx framework version",
        required=False)
    parser.add_argument("--sysroot", help="Path to the sysroot", required=True)
    parser.add_argument(
        "--lib-dir",
        help="Path to the location of shared libraries",
        action='append',
        default=[])
    # This forces a recompile when the CIPD version changes. The value is unused.
    parser.add_argument(
        "--cipd-version", help="CIPD version of Rust toolchain", required=False)
    parser.add_argument
    args = parser.parse_args()

    env = os.environ.copy()
    create_base_directory(args.out_dir)

    clang_c_compiler = os.path.join(args.clang_prefix, "clang")
    configure_triple(args.target, args, clang_c_compiler, env)
    if args.host != args.target:
        configure_triple(args.host, args, clang_c_compiler, env)

    env["CARGO_TARGET_DIR"] = args.out_dir
    env["CARGO_BUILD_DEP_INFO_BASEDIR"] = args.out_dir
    env["RUSTC"] = args.rustc
    env["RUST_BACKTRACE"] = "1"
    env["CC"] = clang_c_compiler
    if args.sysroot:
        env["CFLAGS"] = " ".join(
            ["--sysroot=" + args.sysroot] +
            ["-L" + dir for dir in args.lib_dir])
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

    # Save the cargo arguments so that tooling can properly pick them up. This is so that these
    # arguments can be changed here without having to go change the tooling scripts as well.
    # Remove the `cargo build` part of the args, and the message format argument, since tooling
    # will want to control those itself.
    cargo_args = call_args[2:]

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
        lib_path = None
        for filename in data["filenames"]:
            # prefer .rlibs to .sos or .dylibs
            if filename.endswith(".rlib"):
                lib_path = filename
                break
            if filename.endswith(".so") or filename.endswith(".dylib"):
                lib_path = filename
        if lib_path is None:
            continue

        # For libraries built for both the host and the target, pick
        # the one being built for the target.
        # If we've already seen a lib with the given ID and the new one doesn't
        # contain our target, discard it and keep the current entry.
        if (crate_id in crate_id_to_info) and (args.target not in lib_path):
            continue

        crate_name = data["target"]["name"]

        # Build scripts built for our dependencies unfortunately have the same
        # package ID as the crates themselves. In order to distinguish them,
        # we look for the crate name that cargo uses for these artifacts,
        # "build-script-build".
        if crate_name == "build-script-build":
            continue

        if crate_name != "fuchsia-third-party":
            # go from e.g. target/debug/deps/libfoo.rlib to target/debug/deps
            deps_folders.add(os.path.dirname(lib_path))

        crate_id_to_info[crate_id] = {
            "crate_name": crate_name.replace('-', '_'),
            "lib_path": lib_path,
        }

    cargo_lock_path = os.path.join(args.crate_root, "Cargo.lock")
    with open(cargo_lock_path, "r") as file:
        cargo_lock_toml = pytoml.load(file)

    # output the info for fuchsia-third-party's direct dependencies
    crates = {}
    cargo_dependencies = cargo_toml["dependencies"]
    fuchsia_target_spec = 'cfg(target_os = "fuchsia")'
    non_fuchsia_target_spec = 'cfg(not(target_os = "fuchsia"))'
    if "fuchsia" in args.target:
        target_spec = fuchsia_target_spec
        non_target_spec = non_fuchsia_target_spec
    else:
        target_spec = non_fuchsia_target_spec
        non_target_spec = fuchsia_target_spec
    target_only_deps = cargo_toml \
            .get("target", {}) \
            .get(target_spec, {}) \
            .get("dependencies", [])
    cargo_dependencies.update(target_only_deps)
    other_target_deps = cargo_toml \
            .get("target", {}) \
            .get(non_target_spec, {}) \
            .get("dependencies", [])
    for package in cargo_lock_toml["package"]:
        if package["name"] == "fuchsia-third-party":
            for crate_id in package["dependencies"]:
                crate_id = pathless_crate_id(crate_id)
                if crate_id in crate_id_to_info:
                    crate_info = crate_id_to_info[crate_id]
                    crate_name = crate_info["crate_name"]
                    package_name = package_name_from_crate_id(crate_id)
                    if package_name in cargo_dependencies:
                        crate_info[
                            "cargo_dependency_toml"] = cargo_dependencies[
                                package_name]

                        # Move the library into the top level out_dir at:
                        # `{out_dir}/lib{crate_name}-{package_name}.{ext}`
                        # This keeps the path the library stable between invocations of cargo
                        # on different machines with different root paths, which affect
                        # the suffix hash of the crate (in `lib{crate_name}-{hash}.{ext}`).
                        # This makes it possible for GN to know where to look for the library
                        # without first running this script.
                        old_lib_path = crate_info["lib_path"]
                        old_path_split = os.path.splitext(old_lib_path)
                        old_path_prefix = old_path_split[0]
                        ext = old_path_split[1]  # save .rlib/.so/.a
                        new_filename = "lib" + crate_name + "-" + package_name + ext
                        new_lib_path = os.path.join(args.out_dir, new_filename)
                        os.rename(old_lib_path, new_lib_path)

                        # If the artifact was an .rlib and there also exists a corresponding
                        # .rmeta, we have to move that as well.
                        if ext == ".rlib":
                            old_meta_path = old_path_prefix + ".rmeta"
                            if os.path.exists(old_meta_path):
                                new_meta_name = "lib" + crate_name + "-" + package_name + ".rmeta"
                                new_meta_path = os.path.join(
                                    args.out_dir, new_meta_name)
                                os.rename(old_meta_path, new_meta_path)

                        crate_info["lib_path"] = new_lib_path

                        crates[package_name] = crate_info
                    elif package_name not in other_target_deps:
                        print(
                            package_name +
                            " not found in Cargo.toml dependencies section")
                        return 1

    # normalize paths for patches
    patches = cargo_toml["patch"]["crates-io"]
    for patch in patches:
        path = patches[patch]["path"]
        path = os.path.join(args.crate_root, path)
        patches[patch] = {"path": path}

    create_base_directory(args.out_deps_data)
    with open(args.out_deps_data, "w") as file:
        file.write(
            json.dumps(
                {
                    "crates": crates,
                    "deps_folders": list(deps_folders),
                    "patches": patches,
                    "cargo_args": cargo_args
                },
                sort_keys=True,
                indent=4,
                separators=(",", ": ")))  # for humans


if __name__ == '__main__':
    sys.exit(main())
