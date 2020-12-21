#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import json
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Finds and runs tests affected by current change")
    parser.add_argument(
        "--verbose", action="store_true", help="Print verbose messages")
    parser.add_argument(
        "--dry-run", action="store_true", help="Don't run affected tests")
    parser.add_argument(
        "test_args", nargs=argparse.REMAINDER, help="Arguments for `fx test`")
    parser.add_argument(
        "--out-dir", required=True, help="Path to the Fuchsia build directory")
    args = parser.parse_args()

    # Find all modified files
    upstream = (
        subprocess.run(
            [
                "git", "rev-parse", "--abbrev-ref", "--symbolic-full-name",
                "@{u}"
            ],
            capture_output=True,
            encoding="UTF-8",
        ).stdout.strip() or "origin/master")
    local_commit = subprocess.check_output(
        ["git", "rev-list", "HEAD", upstream, "--"],
        encoding="UTF-8").splitlines()[0]
    diff_base = (
        local_commit and subprocess.run(
            ["git", "rev-parse", local_commit + '^'],
            capture_output=True,
            encoding="UTF-8").stdout.strip() or "HEAD")
    modified = subprocess.check_output(
        ["git", "diff", "--name-only", diff_base], encoding="UTF-8")
    if not modified:
        print("No modified files")
        return 0
    if args.verbose:
        print("Modified files:")
        print(modified)

    # Ensure that tests.json is fresh by regenerating Ninja if needed
    subprocess.check_call(["fx", "build", "build.ninja"])

    # Index tests.json
    with open(os.path.join(args.out_dir, "tests.json")) as tests_json:
        tests = json.load(tests_json)

    # Find stamps for all tests
    stamp_to_test_label = {
        "touch " + os.path.join(
            # Transform GN label to stamp file path, for example:
            # //my/gn:path($my_toolchain) -> obj/my/gn/path.stamp
            "obj",
            label[2:].partition("(")[0].replace(":", "/") + ".stamp"): label
        for label in (entry["test"]["label"] for entry in tests)
    }

    # Touch all modified files so they're newer than any stamps
    git_base = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], encoding="UTF-8").strip()
    file_stats = {}
    for path in modified.splitlines():
        p = os.path.join(git_base, path)
        if not p.endswith("BUILD.gn") and os.path.exists(p):
            stat = os.stat(p)
            file_stats[p] = stat.st_atime, stat.st_mtime
            os.utime(p)

    # Find all stale stamps
    ninja = subprocess.check_output(
        ["fx", "ninja", "-C", args.out_dir, "-n", "-v"],
        encoding="UTF-8",
        stderr=subprocess.STDOUT,
    ).splitlines()
    stale_stamps = [line.partition("] ")[2] for line in ninja]

    # Restore timestamps
    for p, stat in file_stats.items():
        os.utime(p, stat)

    # Check stale stamps against test labels
    affected_labels = {
        stamp_to_test_label.get(stamp, None) for stamp in stale_stamps
    }
    affected_labels.discard(None)

    if not affected_labels:
        if args.verbose:
            print("No affected tests")
        return 0
    if args.verbose:
        print("Affected tests:")
        print("\n".join(sorted(affected_labels)))
        print()

    # Run affected tests
    if args.dry_run:
        if args.verbose:
            print("Not running tests (--dry-run)")
        return 0

    return subprocess.run(
        ["fx", "test"] + sorted(affected_labels) + args.test_args,
    ).returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as e:
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(0)
