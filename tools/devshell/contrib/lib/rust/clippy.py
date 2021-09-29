#!/usr/bin/env python3.8
#
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs `clippy` on a set of gn targets or rust source files

import argparse
import os
from pathlib import Path
import subprocess
import sys

import rust
from rust import (
    FUCHSIA_BUILD_DIR,
    HOST_PLATFORM,
    PREBUILT_THIRD_PARTY_DIR as THIRD_PARTY,
)


def main():
    parser = argparse.ArgumentParser(
        description="Run cargo clippy on a set of targets or rust files"
    )
    parser.add_argument("-v", help="verbose", action="store_true", default=False)
    parser.add_argument(
        "input",
        nargs="+",
        default=[],
    )
    parser.add_argument(
        "--files",
        action="store_true",
        help="treat the inputs as source files rather than gn targets",
    )
    parser.add_argument("--out-dir", help="Path to the Fuchsia build directory")
    args = parser.parse_args()

    build_dir = Path(args.out_dir) if args.out_dir else FUCHSIA_BUILD_DIR
    jq = [THIRD_PARTY / "jq" / HOST_PLATFORM / "bin" / "jq", "-s", "-r"]
    ninja = [THIRD_PARTY / "ninja" / HOST_PLATFORM / "ninja", "-C", build_dir]

    if args.files:
        files = [os.path.relpath(f, build_dir) for f in args.input]
        clippy_outputs = set()
        try:
            targets = subprocess.check_output(ninja + ["-t", "query"] + files)
        except subprocess.CalledProcessError as e:
            return 1
        for path in targets.splitlines():
            path = path.decode("utf-8")
            if path.endswith(".clippy"):
                clippy_outputs.add(path.strip())
        clippy_outputs = list(clippy_outputs)

        if args.v:
            print("found the following targets for those source files:")
            print("\n".join(clippy_outputs))
            print()

        # Because clippy is run on targets, not individual files, we have to do some filtering
        # to pull out the specific lints for the requested files. This query collates lints from
        # every target that uses one of the requested source files, deduplicates them (necessary
        # because the same file can be used in multiple targets), and extracts only the lints whose
        # spans have filenames that match one of the inputs.
        query = " or ".join(
            '.file_name == "{}"'.format(os.path.relpath(Path(f).resolve(), build_dir))
            for f in args.input
        )
        full_query = " | ".join(
            [
                "unique[]",
                "select(.spans | length > 0)",
                "select(.spans | first | {})".format(query),
                ".rendered",
            ]
        )
        subprocess.run(ninja + clippy_outputs)
        subprocess.run(jq + [full_query] + [build_dir / p for p in clippy_outputs])
    else:
        targets = [rust.GnTarget(t + ".clippy") for t in args.input]
        subprocess.run(ninja + [t.ninja_target for t in targets])
        clippy_outputs = [t.gen_dir.joinpath(t.label_name) for t in targets]
        subprocess.run(jq + ["unique[] | .rendered"] + clippy_outputs)


if __name__ == "__main__":
    sys.exit(main())
