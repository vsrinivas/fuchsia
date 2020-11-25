#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to generate runtimes.json for Clang toolchain

This script invokes Clang with different flags to discover the corresponding
runtime paths. The script also ensures that every runtime is stripped and if
not strips it into the .build-id directory, and optionally also generates a
breakpad file. Finally, it prints runtimes.json consumed by the build.
"""

# TODO(phosek): consider rewriting this script in Go reusing the existing logic
# in //tools/debug to populate .build-id directory and generate breakpad.

import argparse
import json
import os
import re
import shutil
import subprocess
import sys


ELF_MAGIC = b"\x7fELF"

TARGETS = ["x86_64-unknown-fuchsia", "aarch64-unknown-fuchsia"]
TRIPLE_TO_TARGET = {
    "x86_64-unknown-fuchsia": "x64",
    "aarch64-unknown-fuchsia": "arm64",
}

# TODO(phosek): use `clang --target=... -print-multi-lib` instead of hardcoding
# these once it supports all variants.
CFLAGS = [[], ["-fsanitize=address"], ["-fsanitize=undefined"]]
LDFLAGS = [[], ["-static-libstdc++"]]


def trace_link(clang_dir, target, sysroot, cflags, ldflags):
    cmd = [
        os.path.join(clang_dir, "bin", "clang++"),
        "--target=%s" % target,
        "--sysroot=%s" % sysroot,
        "-xc++",
        "-",
        "-o",
        "/dev/null",
        "-Wl,--trace",
    ]
    if cflags:
        cmd.extend(cflags)
    if ldflags:
        cmd.extend(ldflags)
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    outs, errs = p.communicate(input=b"int main() {}")
    return outs.decode().splitlines()


def read_soname_and_build_id(readelf, filename):
    p = subprocess.Popen(
        [readelf, "-Wnd", filename], stdout=subprocess.PIPE, env={"LC_ALL": "C"}
    )
    outs, _ = p.communicate()
    if p.returncode != 0:
        raise Exception("failed to read notes")
    match = re.search(r"Library soname: \[([a-zA-Z0-9.+_-]+)\]", outs.decode())
    soname = match.group(1) if match else None
    match = re.search(r"Build ID: ([a-zA-Z0-9_-]+)", outs.decode())
    if not match:
        raise Exception("build ID missing")
    build_id = match.group(1)
    return soname, build_id


def generate_entry(filename, clang_dir, build_id_dir, dump_syms):
    entry = {"dist": os.path.relpath(filename, clang_dir)}
    soname, build_id = read_soname_and_build_id(
        os.path.join(clang_dir, "bin", "llvm-readelf"), filename
    )
    if soname:
        entry["soname"] = soname

    if not build_id_dir:
        return entry

    dist_file = build_id_dir + "/%s/%s" % (build_id[0:2], build_id[2:])
    debug_file = dist_file + ".debug"
    os.makedirs(os.path.dirname(debug_file), exist_ok=True)
    if not os.path.exists(debug_file):
        try:
            os.link(filename, debug_file)
        except OSError, e:
            if e.errno == errno.EXDEV:
                shutil.copyfile(filename, debug_file)
            else:
                raise e
    if not os.path.exists(dist_file):
        subprocess.check_call(
            [
                os.path.join(clang_dir, "bin", "llvm-objcopy"),
                "--strip-all",
                debug_file,
                dist_file,
            ]
        )
    entry["dist"] = dist_file
    entry["debug"] = debug_file

    if not dump_syms:
        return entry

    breakpad_file = build_id_dir + ".sym"
    if not os.path.exists(breakpad_file):
        with open(breakpad_file, "w") as f:
            soname = soname or os.path.basename(filename)
            subprocess.check_call(
                [dump_syms, "-r", "-n", soname, "-o", "Fuchsia", debug_file], stdout=f,
            )
    entry["breakpad"] = breakpad_file

    return entry


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang-toolchain", required=True, help="path to Clang")
    parser.add_argument("--fuchsia-sdk", required=True, help="path to Fuchsia SDK")
    parser.add_argument("--build-id-dir", help="path .build-id directory")
    parser.add_argument("--dump-syms", help="path to Breakpad dump_syms utility")
    args = parser.parse_args()

    clang_dir = os.path.abspath(args.clang_toolchain)
    build_id_dir = os.path.abspath(args.build_id_dir) if args.build_id_dir else None
    if build_id_dir and build_id_dir.startswith(clang_dir):
        build_id_dir = os.path.relpath(build_id_dir, clang_dir)

    runtimes = []
    for target in TARGETS:
        arch = TRIPLE_TO_TARGET[target]
        sysroot = os.path.join(args.fuchsia_sdk, "arch", arch, "sysroot")

        for cflags in CFLAGS:
            for ldflags in LDFLAGS:
                runtime = []
                for lib in trace_link(clang_dir, target, sysroot, cflags, ldflags):
                    lib_path = os.path.abspath(lib)
                    if not os.path.isfile(lib_path):
                        continue
                    with open(lib_path, "rb") as f:
                        magic = f.read(len(ELF_MAGIC))
                    if magic != ELF_MAGIC:
                        continue
                    if not lib_path.startswith(clang_dir):
                        continue
                    runtime.append(
                        generate_entry(
                            lib_path, clang_dir, build_id_dir, args.dump_syms
                        )
                    )
                runtimes.append(
                    {
                        "cflags": cflags,
                        "ldflags": ldflags,
                        "runtime": runtime,
                        "target": [target],
                    }
                )

    json.dump(runtimes, sys.stdout, indent=2, sort_keys=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
