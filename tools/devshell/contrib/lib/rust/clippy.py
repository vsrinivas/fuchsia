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
    generated_file = build_dir / "clippy_target_mapping.json"

    if args.all:
        clippy_targets = get_targets(generated_file, set(), build_dir, get_all=True)
    elif args.files:
        input_files = {os.path.relpath(f, build_dir) for f in args.input}
        clippy_targets = get_targets(generated_file, input_files, build_dir)
        if args.verbose and not args.get_outputs:
            print("Found the following targets for those source files:")
            print(*(t.gn_target for t in clippy_targets), sep="\n")
    else:
        clippy_targets = []
        for target in args.input:
            gn_target = rust.GnTarget(target, args.fuchsia_dir)
            gn_target.label_name += ".clippy"
            clippy_targets.append(gn_target)

    output_files = [
        os.path.relpath(t.gen_dir(build_dir).joinpath(t.label_name), build_dir)
        for t in clippy_targets
    ]
    if args.get_outputs:
        print(*output_files, sep="\n")
        return 0
    if not output_files:
        print("Error: Couldn't find any clippy outputs for those inputs")
        return 1
    if not args.no_build:
        try:
            build_targets(output_files, build_dir, args.fuchsia_dir, args.verbose)
        except subprocess.CalledProcessError:
            return 1

    lints = {}
    for clippy_output in output_files:
        with open(build_dir / clippy_output) as f:
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
    subprocess.run(ninja + output_files, stdout=sys.stderr, check=True)


def get_targets(source_map, input_files, build_dir, get_all=False):
    targets = set()
    with open(source_map) as f:
        raw = json.load(f)
    for target in raw:
        clippy_target = rust.GnTarget(target["clippy"], build_dir)
        if get_all or any(f in input_files for f in target["src"]):
            targets.add(clippy_target)
    return targets


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run cargo clippy on a set of targets or rust files"
    )
    parser.add_argument(
        "--verbose", "-v", help="verbose", action="store_true", default=False
    )
    parser.add_argument(
        "--files",
        "-f",
        action="store_true",
        help="treat the inputs as source files rather than gn targets",
    )
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("input", nargs="*", default=[])
    inputs.add_argument("--all", action="store_true", help="run on all clippy targets")
    advanced = parser.add_argument_group("advanced")
    advanced.add_argument("--out-dir", help="path to the Fuchsia build directory")
    advanced.add_argument("--fuchsia-dir", help="path to the Fuchsia root directory")
    advanced.add_argument(
        "--raw",
        action="store_true",
        help="emit full json rather than human readable messages",
    )
    advanced.add_argument(
        "--get-outputs",
        action="store_true",
        help="emit a list of clippy output files rather than lints",
    )
    advanced.add_argument(
        "--no-build",
        action="store_true",
        help="don't build the clippy output, instead expect that it already exists",
    )
    return parser.parse_args()


if __name__ == "__main__":
    sys.exit(main())
