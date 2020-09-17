#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### generates documentation for a Rust target

import argparse
import os
import platform
import subprocess
import sys

import rust
from rust import ROOT_PATH

def manifest_path_from_path_or_gn_target(arg):
    if arg.endswith("Cargo.toml"):
        return os.path.abspath(arg)
    else:
        gn_target = rust.GnTarget(arg)
        return gn_target.manifest_path()

def main():
    parser = argparse.ArgumentParser("Compiles all third-party Rust crates")
    parser.add_argument("manifest_path",
                        metavar="gn_target",
                        type=manifest_path_from_path_or_gn_target,
                        help="GN target to document. \
                              Use '.[:target]' to discover the cargo target \
                              for the current directory or use the \
                              absolute path to the target \
                              (relative to $FUCHSIA_DIR). \
                              For example: //garnet/bin/foo/bar:baz. \
                              Alternatively, this can be a path to a \
                              Cargo.toml file of a package for which to \
                              generate docs.")
    parser.add_argument("--target",
                        help="Target triple for which this crate is being compiled",
                        default="x86_64-fuchsia")
    parser.add_argument("--out-dir",
                        help="Path to the Fuchsia build directory",
                        required=False)
    parser.add_argument("--no-deps",
                        action="store_true",
                        help="Disable building of docs for dependencies")
    parser.add_argument("--doc-private",
                        action="store_true",
                        help="Document private items")
    parser.add_argument("--open",
                        action="store_true",
                        help="Open the generated documentation")

    args = parser.parse_args()

    if args.out_dir:
        build_dir = args.out_dir
    else:
        build_dir = os.environ["FUCHSIA_BUILD_DIR"]

    env = os.environ.copy()

    host_platform = "%s-%s" % (
        platform.system().lower().replace("darwin", "mac"),
        {
            "x86_64": "x64",
            "aarch64": "arm64",
        }[platform.machine()],
    )

    target_cpu = {
        "x86_64-fuchsia": "x64",
        "aarch64-fuchsia": "aarch64",
        "x86_64-unknown-linux-gnu": "x64",
        "aarch64-unknown-linux-gnu": "aarch64",
    }[args.target]

    # run cargo from third_party/rust_crates which has an appropriate .cargo/config
    cwd = os.path.join(ROOT_PATH, "third_party", "rust_crates")
    buildtools_dir = os.path.join(ROOT_PATH, "prebuilt", "third_party")
    clang_prefix = os.path.join(buildtools_dir, "clang", host_platform, "bin")
    cmake_dir = os.path.join(buildtools_dir, "cmake", host_platform, "bin")
    rust_dir = os.path.join(buildtools_dir, "rust", host_platform, "bin")
    cargo = os.path.join(rust_dir, "cargo")
    rustc = os.path.join(rust_dir, "rustc")
    rustdoc = os.path.join(ROOT_PATH, "scripts", "rust", "rustdoc_no_ld_library_path.sh")

    shared_libs_root = os.path.join(ROOT_PATH, build_dir)
    sysroot = os.path.join(
        ROOT_PATH, build_dir, "zircon_toolchain", "obj", "zircon", "public",
        "sysroot", "sysroot")

    clang_c_compiler = os.path.join(clang_prefix, "clang")

    env["CARGO_TARGET_LINKER"] = clang_c_compiler
    env["CARGO_TARGET_X86_64_APPLE_DARWIN_LINKER"] = clang_c_compiler
    env["CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER"] = clang_c_compiler
    env["CARGO_TARGET_%s_LINKER" % args.target.replace("-", "_").upper()] = clang_c_compiler
    if "fuchsia" in args.target:
        env["CARGO_TARGET_%s_RUSTFLAGS" % args.target.replace("-", "_").upper()] = (
            "-Clink-arg=--target=" + args.target +
            " -Clink-arg=--sysroot=" + sysroot +
            " -Lnative=" + shared_libs_root
        )
    else:
        env["CARGO_TARGET_%s_RUSTFLAGS" % args.target.replace("-", "_").upper()] = (
            "-Clink-arg=--target=" + args.target
        )
    env["RUSTC"] = rustc
    env["RUSTDOC"] = rustdoc
    env["RUSTDOCFLAGS"] = "-Z unstable-options --enable-index-page"
    env["RUST_BACKTRACE"] = "1"
    env["CC"] = clang_c_compiler
    if "fuchsia" in args.target:
        env["CFLAGS"] = "--sysroot=%s -L %s" % (sysroot, shared_libs_root)
    env["CXX"] = os.path.join(clang_prefix, "clang++")
    env["AR"] = os.path.join(clang_prefix, "llvm-ar")
    env["RANLIB"] = os.path.join(clang_prefix, "llvm-ranlib")
    env["PATH"] = "%s:%s" % (env["PATH"], cmake_dir)

    call_args = [
        cargo,
        "doc",
        "--manifest-path=%s" % args.manifest_path,
        "--target=%s" % args.target,
    ]

    if args.no_deps:
        call_args.append("--no-deps")

    if args.open:
        call_args.append("--open")

    if args.doc_private:
        call_args.append("--document-private-items")

    return subprocess.call(call_args, env=env, cwd=cwd)

if __name__ == '__main__':
    sys.exit(main())
