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

# Fixes the target path in the given depfile.
def fix_depfile(depfile_path, output_path, base_path):
    with open(depfile_path, "r+") as depfile:
        with open(output_path, "w+") as output_file:
            content = depfile.read()
            content_split = content.split(': ', 1)
            target_path = content_split[0]
            adjusted_target_path = os.path.relpath(target_path, start=base_path)
            new_content = "%s: %s" % (adjusted_target_path, content_split[1])
            output_file.write(new_content)

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
    parser.add_argument("--fidl-gen-dir",
                        help="Path to the root fidl gen directory",
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
    parser.add_argument("--cmake-dir",
                        help="Path to the directory containing cmake",
                        required=True)
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
        env["CARGO_TARGET_LINKER"] = clang_c_compiler
        env["CARGO_TARGET_X86_64_APPLE_DARWIN_LINKER"] = clang_c_compiler
        env["CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER"] = clang_c_compiler
        env["CARGO_TARGET_%s_LINKER" % args.target_triple.replace("-", "_").upper()] = clang_c_compiler
        env["CARGO_TARGET_%s_RUSTFLAGS" % args.target_triple.replace("-", "_").upper()] = "-Clink-arg=--target=" + args.target_triple + " -Clink-arg=--sysroot=" + args.sysroot + " -Lnative=" + args.shared_libs_root
    env["CARGO_TARGET_DIR"] = args.out_dir
    env["CARGO_BUILD_DEP_INFO_BASEDIR"] = args.root_out_dir
    env["RUSTC"] = args.rustc
    env["RUST_BACKTRACE"] = "1"
    env["FUCHSIA_GEN_ROOT"] = args.root_gen_dir
    env["FIDL_GEN_ROOT"] = args.fidl_gen_dir
    env["CC"] = clang_c_compiler
    env["CXX"] = args.clang_prefix + '/clang++'
    env["AR"] = args.clang_prefix + '/llvm-ar'
    env["RANLIB"] = args.clang_prefix + '/llvm-ranlib'
    env["PATH"] = "%s:%s" % (env["PATH"], args.cmake_dir)

    target_directory = os.path.join(args.gen_dir, "target")
    create_base_directory(target_directory)

    call_args = [
        args.cargo,
        "build",
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

    if args.with_tests:
        call_args.append("--tests")

    call_args.append("--message-format=json")

    retcode, stdout, stderr = run_command(call_args, env, args.crate_root)
    if retcode != 0:
        # The output is not particularly useful as it is formatted in JSON.
        # Re-run the command with a user-friendly format instead.
        del call_args[-1]
        _, stdout, stderr = run_command(call_args, env, args.crate_root)
        print(stdout + stderr)
        return retcode

    # Fix the depfile manually until a flag gets added to cargo to tweak the
    # base path for targets.
    # Note: out_dir already contains the "target.rust" directory.
    # Note: the depfile is written to a new path, as cargo re-writes depfiles
    # for all dependencies of a target on every build, which means that a bin
    # build might "update" an already built depfile and corrupt the paths
    # contained within.
    output_name = args.name
    if args.type == "lib":
        output_name = "lib%s" % args.name
    build_type = "release" if args.release else "debug"
    depfile_path = os.path.join(args.out_dir, args.target_triple, build_type,
                                "%s.d" % output_name)
    fix_depfile(depfile_path, os.path.join(args.out_dir, os.path.basename(depfile_path)), args.root_out_dir)

    for line in stdout.splitlines():
        data = json.loads(line)

        if not args.with_tests:
            continue
        generated_test_path = None
        test_name = None
        if not ("profile" in data and data["profile"]["test"]):
            continue
        generated_test_path = data["filenames"][0]
        # only integration tests are marked as 'test', unit tests are 'lib'
        test_type = 'unit'
        if data["target"]["kind"][0] == "test":
            test_type = 'integration'
            if "target" in data and data["target"]["name"]:
                test_name = data["target"]["name"]
        if generated_test_path and test_type:
            if test_name:
                dest_test_path = os.path.join(args.out_dir,
                        "%s-%s-%s-%s-test" % (args.name, test_name, args.type, test_type))
            else:
                # maintain support for unit tests
                dest_test_path = os.path.join(args.out_dir,
                        "%s-%s-%s-test" % (args.name, args.type, test_type))
            if os.path.islink(dest_test_path) and os.readlink(dest_test_path) != generated_test_path:
                os.unlink(dest_test_path)
            if not os.path.exists(dest_test_path):
                os.symlink(generated_test_path, dest_test_path)

    return 0


if __name__ == '__main__':
    sys.exit(main())
