#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import itertools
import json
import os
import string
import subprocess
import sys

ROOT_PATH = os.path.abspath(__file__ + "/../../..")
sys.path += [os.path.join(ROOT_PATH, "third_party", "pytoml")]
import pytoml

# Creates the directory containing the given file.
def create_base_directory(file):
    path = os.path.dirname(file)
    try:
        os.makedirs(path)
    except os.error:
        # Already existed.
        pass

# Returns the list of native libs inherited from the given dependencies.
def extract_native_libs(dependency_infos):
    all_libs = itertools.chain.from_iterable(map(lambda i: i["native_libs"],
                                                 dependency_infos))
    return list(set(all_libs))


# Fixes the target path in the given depfile.
def fix_depfile(depfile_path, base_path):
    with open(depfile_path, "r+") as depfile:
        content = depfile.read()
        content_split = content.split(': ', 1)
        target_path = content_split[0]
        adjusted_target_path = os.path.relpath(target_path, start=base_path)
        new_content = "%s: %s" % (adjusted_target_path, content_split[1])
        depfile.seek(0)
        depfile.write(new_content)
        depfile.truncate()


# Runs the given command and returns its return code and output.
def run_command(args, env, cwd):
    job = subprocess.Popen(args, env=env, cwd=cwd, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
    stdout, stderr = job.communicate()
    return (job.returncode, stdout, stderr)


def main():
    parser = argparse.ArgumentParser("Compiles a Rust crate")
    parser.add_argument("--type",
                        help="Type of artifact to produce",
                        required=True,
                        choices=["lib", "bin"])
    parser.add_argument("--name",
                        help="Name of the artifact to produce",
                        required=True)
    parser.add_argument("--out-dir",
                        help="Path to the output directory",
                        required=True)
    parser.add_argument("--gen-dir",
                        help="Path to the target's generated source directory",
                        required=True)
    parser.add_argument("--root-out-dir",
                        help="Path to the root output directory",
                        required=True)
    parser.add_argument("--root-gen-dir",
                        help="Path to the root gen directory",
                        required=True)
    parser.add_argument("--crate-root",
                        help="Path to the crate root",
                        required=True)
    parser.add_argument("--cargo",
                        help="Path to the cargo tool",
                        required=True)
    parser.add_argument("--sysroot",
                        help="Path to the sysroot",
                        required=False)
    parser.add_argument("--clang_prefix",
                        help="Path to the clang prefix",
                        required=False)
    parser.add_argument("--rustc",
                        help="Path to the rustc binary",
                        required=True)
    parser.add_argument("--target-triple",
                        help="Compilation target",
                        required=True)
    parser.add_argument("--release",
                        help="Build in release mode",
                        action="store_true")
    parser.add_argument("--frozen",
                        help="Pass --frozen to cargo when building",
                        default = False,
                        action="store_true")
    parser.add_argument("--label",
                        help="Label of the target to build",
                        required=True)
    parser.add_argument("--cmake-dir",
                        help="Path to the directory containing cmake",
                        required=True)
    parser.add_argument("--vendor-directory",
                        help="Path to the vendored crates",
                        required=True)
    parser.add_argument("--deps",
                        help="List of dependencies",
                        nargs="*")
    parser.add_argument("--shared-libs-root",
                        help="Path to the location of shared libraries",
                        required=True)
    parser.add_argument("--with-tests",
                        help="Whether to generate unit tests too",
                        action="store_true")
    args = parser.parse_args()

    env = os.environ.copy()
    clang_c_compiler = args.clang_prefix + '/clang'
    if args.sysroot is not None:
        env["CARGO_TARGET_%s_LINKER" % args.target_triple.replace("-", "_").upper()] = clang_c_compiler
        env["CARGO_TARGET_%s_RUSTFLAGS" % args.target_triple.replace("-", "_").upper()] = "-Clink-arg=--target=" + args.target_triple + " -Clink-arg=--sysroot=" + args.sysroot
    env["CARGO_TARGET_DIR"] = args.out_dir
    env["RUSTC"] = args.rustc
    env["RUST_BACKTRACE"] = "1"
    env["FUCHSIA_GEN_ROOT"] = args.root_gen_dir
    env["CC"] = clang_c_compiler
    env["CXX"] = args.clang_prefix + '/clang++'
    env["AR"] = args.clang_prefix + '/llvm-ar'
    env["RANLIB"] = args.clang_prefix + '/llvm-ranlib'
    env["PATH"] = "%s:%s" % (env["PATH"], args.cmake_dir)

    # Generate Cargo.toml.
    original_manifest = os.path.join(args.crate_root, "Cargo.toml")
    target_directory = os.path.join(args.gen_dir, "target")
    create_base_directory(target_directory)
    package_name = None
    with open(original_manifest, "r") as manifest:
        config = pytoml.load(manifest)
        package_name = config["package"]["name"]
        default_name = package_name.replace("-", "_")

    if args.type == "lib":
        # Since the generated .rlib artifact won't actually be used (for now),
        # just do syntax checking and avoid generating it.
        build_command = "check"
    else:
        build_command = "build"

    call_args = [
        args.cargo,
        build_command,
        "--target=%s" % args.target_triple,
        "--verbose",
    ]

    if args.frozen:
        call_args.append("--frozen")
    if args.release:
        call_args.append("--release")
    if args.type == "lib":
        call_args.append("--lib")
    if args.type == "bin":
        call_args.extend(["--bin", args.name])
    retcode, stdout, stderr = run_command(call_args, env, args.crate_root)
    if retcode != 0:
        print(stdout + stderr)
        return retcode

    # Fix the depfile manually until a flag gets added to cargo to tweak the
    # base path for targets.
    # Note: out_dir already contains the "target.rust" directory.
    output_name = args.name
    if args.type == "lib":
        output_name = "lib%s" % args.name
    build_type = "release" if args.release else "debug"
    depfile_path = os.path.join(args.out_dir, args.target_triple, build_type,
                                "%s.d" % output_name)
    fix_depfile(depfile_path, args.root_out_dir)

    if args.with_tests:
        test_args = list(call_args)
        test_args[1] = "test"
        test_args.append("--no-run")
        test_args.append("--message-format=json")
        retcode, stdout, _ = run_command(test_args, env, args.crate_root)
        if retcode != 0:
            # The output is not particularly useful as it is formatted in JSON.
            # Re-run the command with a user-friendly format instead.
            del test_args[-1]
            _, stdout, stderr = run_command(test_args, env, args.crate_root)
            print(stdout + stderr)
            return retcode
        generated_test_path = None
        for line in stdout.splitlines():
            data = json.loads(line)
            if "profile" in data and data["profile"]["test"]:
              generated_test_path = data["filenames"][0]
              break
        if not generated_test_path:
            print("Unable to locate resulting test file")
            return 1
        dest_test_path = os.path.join(args.out_dir,
                                      "%s-%s-test" % (args.name, args.type))
        if os.path.islink(dest_test_path):
            os.unlink(dest_test_path)
        os.symlink(generated_test_path, dest_test_path)

    return 0


if __name__ == '__main__':
    sys.exit(main())
