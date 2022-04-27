#!/usr/bin/env fuchsia-vendored-python

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Utilities for working with firmware prebuilts in CIPD.

Firmware is often built out-of-tree in a standalone repo, then uploaded to CIPD
as prebuilt artifacts, then pulled into the Fuchsia tree via jiri manifest.

The exact workflows may differ for various targets, but this script exposes
some common target-agnostic utilities for working with these CIPD packages.
"""

import argparse
import json
import tempfile
import os
import re
import subprocess
import sys
from typing import Dict, Iterable, Optional

_MY_DIR = os.path.dirname(__file__)
_FUCHSIA_ROOT = os.path.normpath(os.path.join(_MY_DIR, "..", "..", ".."))

# For now assume `git` and `repo` live somewhere on $PATH.
_GIT_TOOL = "git"
_REPO_TOOL = "repo"

# `cipd` is available as a prebuilt in the Fuchsia checkout.
_CIPD_TOOL = os.path.join(_FUCHSIA_ROOT, ".jiri_root", "bin", "cipd")


class Git:
    """Wraps operations on a git repo."""

    def __init__(self, repo_path: str):
        """Initializes a Git object.

        Args:
            repo_path: path to git repo root.
        """
        self.repo_path = repo_path

    def git(
            self,
            command: Iterable[str],
            check=True,
            capture_output=True) -> subprocess.CompletedProcess:
        """Calls `git` in this repo.

        Args:
            command: git command to execute.
            check: passed to subprocess.run().
            capture_output: True to capture output, False to let it through to
                            the calling terminal.

        Returns:
            The resulting subprocess.CompletedProcess object.
        """
        return subprocess.run(
            [_GIT_TOOL, "-C", self.repo_path] + command,
            check=check,
            text=True,
            capture_output=capture_output)

    def changelog(self, start: Optional[str], end: str) -> str:
        """Returns the additive changelog between two revisions.

        An additive changelog only contains the commits that exist in |end| but
        not |start|. If |start| is not a direct ancestor of |end|, there may
        also be commits only in |start|, which can be determined by calling this
        function again with the commit versions reversed.

        Args:
            start: the starting revision, or None to return the entire history.
            end: the ending revision.

        Returns:
            A changelog of commits that exist in |end| but not in |start|.

            The changelog is formatted as "oneline" descriptions of each commit
            separated by newlines.

        Raises:
            subprocess.CalledProcessError: failed to read the git log.
        """
        log_target = f"{start}..{end}" if start else end
        return self.git(["log", "--oneline", log_target]).stdout.strip()


class Repo:
    """Wraps operations on a `repo` checkout."""

    def __init__(self, root):
        """Initializes the Repo object.

        Args:
            root: path to repo root.
        """
        self.root = root
        self.git_repos = self._list_git_repos()

    def _list_git_repos(self):
        """Returns a {name: Git} mapping of all repos in this checkout."""
        # `repo info` gives us the information we need. Output format is:
        #   ---------------
        #   Project: <name>
        #   Mount path: <absolute_path>
        #   Current revision: <revision>
        #   Local Branches: <#> [names]
        #   ---------------
        repo_info = subprocess.run(
            [_REPO_TOOL, "info", "--local-only"],
            cwd=self.root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True)

        matches = re.findall(
            r"Project: (.*?)$\s*Mount path: (.*?)$", repo_info.stdout,
            re.MULTILINE)
        return {m[0]: Git(m[1]) for m in matches}


def get_cipd_version_manifest(package: str, version: str) -> Dict[str, str]:
    """Returns the contents of the manifest.json file in a CIPD package.

    Args:
        package: CIPD package name.
        version: CIPD package version.

    Returns:
        A {name: version} mapping, or empty dict if manifest.json wasn't found.
    """
    with tempfile.TemporaryDirectory() as temp_dir:
        # Download the package so we can read the manifest file.
        subprocess.run(
            [_CIPD_TOOL, "ensure", "-root", temp_dir, "-ensure-file", "-"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
            input=f"{package} {version}")

        try:
            with open(os.path.join(temp_dir, "manifest.json"), "r") as file:
                return json.load(file)
        except FileNotFoundError:
            return {}


def changelog(
        repo: Repo, package: str, old_version: str, new_version: str) -> str:
    """Generates a changelog between the two versions.

    Args:
        repo: Repo object.
        package: CIPD package name.
        old_version: old CIPD version.
        new_version: new CIPD version.

    Returns:
        A changelog formatted for use in a git commit message.
    """
    old_manifest = get_cipd_version_manifest(package, old_version)
    new_manifest = get_cipd_version_manifest(package, new_version)

    # The manifests don't necessarily have the exact same items, repos may be
    # added or removed over time, make sure we track them all.
    all_manifest_repos = set(old_manifest.keys()).union(new_manifest.keys())

    # Track diffs as (repo, revision, added, removed) tuples.
    diffs = []

    for manifest_repo in sorted(all_manifest_repos):
        old_revision = old_manifest.get(manifest_repo, None)
        new_revision = new_manifest.get(manifest_repo, None)
        added, removed = None, None

        if new_revision is None:
            # If the repo has been removed, we can't get history since it won't
            # exist in a ToT checkout.
            removed = "[repo has been removed]"
        else:
            git_repo = repo.git_repos[manifest_repo]
            added = git_repo.changelog(old_revision, new_revision)
            if old_revision:
                # If the old revision wasn't a direct ancestor but was a
                # separate branch, there may be some CLs that do not exist now
                # in the new revision.
                removed = git_repo.changelog(new_revision, old_revision)

        diffs.append([manifest_repo, new_revision, added, removed])

    # Format the changelog to be suitable for a commit message.
    lines = ["-- Changelist --"]
    for name, _, added, removed in diffs:
        if not (added or removed):
            continue

        lines.append(f"{name}:")
        if added:
            lines.append(added)
        if removed:
            lines.append("[removed commits:]")
            lines.append(removed)
        lines.append("")

    lines.append("-- Source Revisions --")
    for name, revision, _, _ in diffs:
        if name in new_manifest:
            lines.append(f"{name}: {revision}")
    return "\n".join(lines)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    subparsers = parser.add_subparsers(dest="action", required=True)

    changelog_parser = subparsers.add_parser(
        "changelog",
        help="Generate a source changelog between two CIPD packages")
    changelog_parser.add_argument("repo", help="Path to the repo root")
    changelog_parser.add_argument("package", help="The CIPD package name")
    changelog_parser.add_argument(
        "old_version", help="The old CIPD version (ref, tag, or ID)")
    changelog_parser.add_argument(
        "new_version", help="The new CIPD version (ref, tag, or ID)")

    return parser.parse_args()


def main() -> int:
    """Script entry point.

    Returns:
        0 on success, non-zero on failure.
    """
    args = _parse_args()

    if args.action == "changelog":
        print(
            changelog(
                Repo(args.repo), args.package, args.old_version,
                args.new_version))

    return 0


if __name__ == "__main__":
    sys.exit(main())
