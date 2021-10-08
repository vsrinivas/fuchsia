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
    prebuilt = PREBUILT_THIRD_PARTY_DIR
    if args.fuchsia_dir:
        prebuilt = Path(args.fuchsia_dir) / "prebuilt" / "third_party"

    ninja = [prebuilt / "ninja" / HOST_PLATFORM / "ninja", "-C", build_dir]
    if args.verbose:
        ninja += ["-v"]

    if args.files or args.files_to_targets:
        input_files = [os.path.relpath(f, build_dir) for f in args.input]
        clippy_outputs = set()
        try:
            targets = subprocess.check_output(ninja + ["-t", "query"] + input_files)
        except subprocess.CalledProcessError:
            return 1
        for path in targets.splitlines():
            path = path.decode("utf-8")
            if path.endswith(".clippy"):
                clippy_outputs.add(path.strip())
        clippy_outputs = list(clippy_outputs)

        if args.files_to_targets:
            print("\n".join(clippy_outputs))
            return 0
        if args.verbose:
            print("found the following targets for those source files:")
            print("\n".join(clippy_outputs) + "\n")

        if not args.no_build:
            subprocess.run(ninja + clippy_outputs, stdout=sys.stderr)
        output_files = [build_dir / out for out in clippy_outputs]
    else:
        targets = []
        for target in args.input:
            gn_target = rust.GnTarget(target, fuchsia_dir=args.fuchsia_dir)
            gn_target.label_name += ".clippy"
            targets.append(gn_target)
        if not args.no_build:
            subprocess.run(ninja + [t.ninja_target for t in targets], stdout=sys.stderr)
        output_files = [t.gen_dir(build_dir).joinpath(t.label_name) for t in targets]

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
