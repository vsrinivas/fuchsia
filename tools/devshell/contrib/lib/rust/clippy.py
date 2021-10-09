#!/usr/bin/env python3.8
#
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs `clippy` on a set of gn targets or rust source files

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

import rust
from rust import FUCHSIA_BUILD_DIR, HOST_PLATFORM, PREBUILT_THIRD_PARTY_DIR


def main():
    args = parse_args()
    build_dir = Path(args.out_dir) if args.out_dir else FUCHSIA_BUILD_DIR

    if args.files or args.files_to_targets:
        input_files = [os.path.relpath(f, build_dir) for f in args.input]
        clippy_targets = files_to_targets(input_files, build_dir)

        if args.files_to_targets:
            print("\n".join(clippy_targets))
            return 0
        if args.verbose:
            print("found the following targets for those source files:")
            print("\n".join(clippy_targets) + "\n")
    else:
        clippy_targets = []
        for target in args.input:
            gn_target = rust.GnTarget(target, args.fuchsia_dir)
            if gn_target.toolchain_suffix:
                print("Clippy doesn't work on non-default toolchains yet")
                continue  # TODO: fxb/591046
            gn_target.label_name += ".clippy"
            clippy_targets.append(gn_target)

    output_files = [t.gen_dir(build_dir).joinpath(t.label_name) for t in clippy_targets]
    if not output_files:
        print("couldn't find any clippy outputs for those inputs")
        return 1
    if not args.no_build:
        build_targets(output_files, build_dir, args.fuchsia_dir, args.verbose)

    lints = {}
    for clippy_output in output_files:
        with open(clippy_output) as f:
            for line in f:
                lint = json.loads(line)
                # filter out "n warnings emitted" messages
                if not lint["spans"]:
                    continue
                # filter out lints for files we didn't ask for
                if args.files and lint["spans"][0]["file_name"] not in input_files:
                    continue
                lints[lint["rendered"]] = lint

    for lint in lints.values():
        print(json.dumps(lint) if args.raw else lint["rendered"])
    if not args.raw:
        print(len(lints), "warning(s) emitted\n")


def build_targets(output_files, build_dir, fuchsia_dir, verbose):
    prebuilt = PREBUILT_THIRD_PARTY_DIR
    if fuchsia_dir:
        prebuilt = Path(fuchsia_dir) / "prebuilt" / "third_party"
    ninja = [prebuilt / "ninja" / HOST_PLATFORM / "ninja", "-C", build_dir]
    if verbose:
        ninja += ["-v"]
    subprocess.run(
        ninja + [os.path.relpath(f, build_dir) for f in output_files], stdout=sys.stderr
    )


def files_to_targets(input_files, build_dir):
    targets = set()
    with open(build_dir / "gen" / "build" / "rust" / "rust_target_mapping.json") as f:
        raw = json.load(f)
    for target in raw:
        clippy_target = rust.GnTarget(target["clippy"], build_dir)
        if clippy_target.toolchain_suffix:
            continue  # TODO run clippy on non-default toolchains: fxb/591046
        if any(f in target["src"] for f in input_files):
            targets.add(clippy_target)
    return targets


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run cargo clippy on a set of targets or rust files"
    )
    parser.add_argument(
        "--verbose", "-v", help="verbose", action="store_true", default=False
    )
    parser.add_argument("input", nargs="+", default=[])
    parser.add_argument(
        "--files",
        "-f",
        action="store_true",
        help="treat the inputs as source files rather than gn targets",
    )
    advanced = parser.add_argument_group("advanced")
    advanced.add_argument("--out-dir", help="path to the Fuchsia build directory")
    advanced.add_argument("--fuchsia-dir", help="path to the Fuchsia root directory")
    advanced.add_argument(
        "--raw",
        action="store_true",
        help="emit full json rather than human readable messages",
    )
    advanced.add_argument(
        "--files-to-targets",
        action="store_true",
        help="emit the list of clippy targets corresponding to a list of rust source files",
    )
    advanced.add_argument(
        "--no-build",
        action="store_true",
        help="never build the clippy output, instead expect that it already exists",
    )
    return parser.parse_args()


if __name__ == "__main__":
    sys.exit(main())
