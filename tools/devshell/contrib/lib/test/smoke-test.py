#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import json
import os
import pathlib
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
            ["git", "rev-parse", local_commit],
            capture_output=True,
            encoding="UTF-8").stdout.strip() or "HEAD")
    modified = subprocess.check_output(
        ["git", "diff", "--name-only", diff_base], encoding="UTF-8")
    if not modified:
        if args.verbose:
            print("No modified files")
        return 0
    if args.verbose:
        print("Modified files:")
        print(modified)

    # Rebuild
    subprocess.check_call(["fx", "build", "updates"])
    # Note: we know all test labels so we could `fx build` only tests.
    # However there is no guarantee that GN labels in tests.json are valid.
    # In fact some of them aren't. o_O
    # Also, `fx build <target>` is broken when <target> begins with "zircon/"
    # and is actually in the Fuchsia GN build, not in the legacy ZN build.

    # Index tests.json
    with open(os.path.join(args.out_dir, "tests.json")) as tests_json:
        tests = json.load(tests_json)

    phony_rule_to_test_name = collections.defaultdict(set)
    for entry in tests:
        label = entry["test"]["label"]
        # Remove leading "//" and toolchain part
        # "//my/gn:path(//build/toolchain:host_x64)" -> "my/gn:path"
        no_toolchain = label[2:].partition("(")[0]
        # Convert label to rule path
        # "my/gn:path" -> "my/gn/path"
        # "foo/bar:bar" -> "foo/bar"
        directory, _, name = no_toolchain.partition(":")
        if not name:
            # "foo/bar" -> "foo/bar"
            rule = directory
        elif directory.rpartition("/")[2] == name:
            # "foo/bar:bar" -> "foo/bar"
            rule = directory
        else:
            # "foo/bar:qux" -> "foo/bar/qux"
            rule = directory + "/" + name
        phony_rule_to_test_name[rule + " is dirty"].add(entry["test"]["name"])

    # Touch all modified files so they're newer than any rules
    git_base = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], encoding="UTF-8").strip()
    for path in modified.splitlines():
        p = pathlib.Path(git_base, path)
        if p.exists() and not path.endswith("BUILD.gn"):
            p.touch()

    # Find all stale rules
    ninja = []
    for buildtype in ("", ".zircon"):
        ninja += subprocess.check_output(
            [
                "fx",
                "ninja",
                "-C",
                args.out_dir + buildtype,
                "-d",
                "explain",
                "-n",
            ],
            encoding="UTF-8",
            stderr=subprocess.STDOUT,
        ).splitlines()

    # Check stale rules against test names
    affected_tests = set()
    for line in ninja:
        if not line.startswith("ninja explain: "):
            continue
        rule = line.partition("phony/")[2]
        if not rule:
            continue
        affected_tests.update(phony_rule_to_test_name[rule])

    if not affected_tests:
        if args.verbose:
            print("No affected tests")
        return 0
    if args.verbose:
        print("Affected tests:")
        print("\n".join(sorted(affected_tests)))
        print()

    # Run affected tests
    if args.dry_run:
        if args.verbose:
            print("Not running tests (--dry-run)")
        return 0

    return subprocess.run(
        ["fx", "test"] + sorted(affected_tests) + args.test_args,
    ).returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as e:
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(0)
