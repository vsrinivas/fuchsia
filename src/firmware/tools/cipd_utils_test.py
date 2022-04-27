#!/usr/bin/env fuchsia-vendored-python

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Unit tests for cipd_utils.py."""

# pylint: disable=missing-class-docstring
# pylint: disable=missing-function-docstring

import os
import subprocess
import textwrap
from typing import List
import unittest
from unittest import mock

import cipd_utils


def get_run_commands(mock_run) -> List[List[str]]:
    """Extracts just the subprocess command list from a mock.

    This just helps reduce boilerplate in the common case where we just
    want to make sure we called the right subprocess command.

    For example, given these calls:
      subprocess.run(["echo", "foo"], check=True, capture_output=True)
      subprocess.run(["echo", "bar"], check=False)
    This would return:
      [["echo", "foo"], ["echo", "bar"]].
    """
    return [mock_args[0] for mock_args, _ in mock_run.call_args_list]


class GitTests(unittest.TestCase):

    @mock.patch.object(cipd_utils.subprocess, "run", autospec=True)
    def test_git_command(self, mock_run):
        git = cipd_utils.Git("path/to/repo/")

        git.git(["foo", "bar"])

        mock_run.assert_called_once_with(
            ["git", "-C", "path/to/repo/", "foo", "bar"],
            check=True,
            text=True,
            capture_output=True)

    @mock.patch.object(cipd_utils.subprocess, "run", autospec=True)
    def test_changelog(self, mock_run):
        git = cipd_utils.Git("path/")
        mock_run.return_value = subprocess.CompletedProcess(None, 0, "fake log")

        result = git.changelog("start_revision", "end_revision")

        self.assertEqual(
            get_run_commands(mock_run), [
                [
                    "git", "-C", "path/", "log", "--oneline",
                    "start_revision..end_revision"
                ]
            ])
        self.assertEqual(result, "fake log")

    @mock.patch.object(cipd_utils.subprocess, "run", autospec=True)
    def test_changelog_no_start(self, mock_run):
        git = cipd_utils.Git("path/")
        mock_run.return_value = subprocess.CompletedProcess(None, 0, "fake log")

        result = git.changelog(None, "end_revision")

        self.assertEqual(
            get_run_commands(mock_run),
            [["git", "-C", "path/", "log", "--oneline", "end_revision"]])
        self.assertEqual(result, "fake log")


# Based off `repo info --local-only` output for repo version v2.21.
_FAKE_REPO_INFO = textwrap.dedent(
    """\
    Manifest branch: main
    Manifest merge branch: refs/heads/main
    Manifest groups: all,-notdefault
    ----------------------------
    Project: foo
    Mount path: /foo
    Current revision: foo_revision
    Manifest revision: main
    Local Branches: 0
    ----------------------------
    Project: bar/baz
    Mount path: /baz
    Current revision: baz_revision
    Manifest revision: baz_revision
    Local Branches: 1 [baz_local_branch]
    ----------------------------
    """)


class RepoTests(unittest.TestCase):

    @mock.patch.object(cipd_utils.subprocess, "run", autospec=True)
    def test_repo_init(self, mock_run):
        mock_run.return_value = subprocess.CompletedProcess(
            None, 0, _FAKE_REPO_INFO)

        repo = cipd_utils.Repo("/repo/root")

        self.assertEqual(
            get_run_commands(mock_run), [["repo", "info", "--local-only"]])
        self.assertEqual(len(repo.git_repos), 2)
        self.assertEqual(repo.git_repos["foo"].repo_path, "/foo")
        self.assertEqual(repo.git_repos["bar/baz"].repo_path, "/baz")


class CipdTests(unittest.TestCase):

    @mock.patch.object(cipd_utils.subprocess, "run", autospec=True)
    def test_repo_init(self, mock_run):

        # The code under test will download the CIPD package into a tempdir
        # so we don't know the path ahead of time. In order to inject a fake
        # manifest, this function will write it to disk as a side-effect of
        # the mock subprocess.run() call.
        def write_cipd_manifest_side_effect(command, *args, **kwargs):
            # The path to download will follow the "-root" arg.
            download_root = command[command.index("-root") + 1]
            with open(os.path.join(download_root, "manifest.json"), "w") as f:
                f.write(
                    textwrap.dedent(
                        """\
                    {
                        "repo1": "revision1",
                        "repo2": "revision2"
                    }
                    """))

        mock_run.side_effect = write_cipd_manifest_side_effect

        manifest = cipd_utils.get_cipd_version_manifest("package", "version")
        self.assertEqual(manifest, {"repo1": "revision1", "repo2": "revision2"})


def set_up_changelog_mocks(
        mock_get_cipd_version_manifest, cipd_version_a, cipd_version_b,
        repo_info):
    """Configures all the mocks needed to produce a changelog.

    There's a lot of mocking that needs to be done to set up the proper
    CIPD, repo, and git calls. Extracting it here allows the tests to
    just focus on the logic.

    Args:
        mock_get_cipd_version_manifest: get_cipd_version_manifest() mock.
        cipd_version_a: CIPD version A.
        cipd_version_b: CIPD version B.
        repo_info: a {name, info} dict of repos to mock out, where |info| is a
                   tuple (revision A, revision B, A->B commits, B->A commits).

    Returns:
        The mock Repo object to use.
    """

    # Mock out the CIPD manifest for the given repos.
    def cipd_version_manifest(_, cipd_version):
        if cipd_version == cipd_version_a:
            git_rev_index = 0
        elif cipd_version == cipd_version_b:
            git_rev_index = 1
        else:
            raise ValueError(f"Unexpected CIPD version {cipd_version}")

        manifest = {}
        for name, info in repo_info.items():
            git_rev = info[git_rev_index]
            if git_rev:
                manifest[name] = git_rev
        return manifest

    mock_get_cipd_version_manifest.side_effect = cipd_version_manifest

    # Mock out the Repo and each Git sub-object.
    mock_repo = mock.Mock(spec=cipd_utils.Repo)
    mock_repo.git_repos = {}

    for name, info in repo_info.items():
        mock_git = mock.Mock(spec=cipd_utils.Git)

        # Python functions are late-binding, meaning the variables will be
        # captured at call-time, not at definition. So we need a wrapper here
        # that we can call immediately to bind the loop variables, or else
        # they'll change on the next loop iteration.
        def wrap_changelog():
            rev_a, rev_b, a_to_b, b_to_a = info

            def changelog(old_revision, new_revision):
                if old_revision == rev_a and new_revision == rev_b:
                    return "\n".join(a_to_b)
                elif old_revision == rev_b and new_revision == rev_a:
                    return "\n".join(b_to_a)
                raise ValueError(
                    f"Unknown {name} revisions {old_revision}, {new_revision}")

            return changelog

        mock_git.changelog.side_effect = wrap_changelog()
        mock_repo.git_repos[name] = mock_git

    return mock_repo


class ChangelogTests(unittest.TestCase):

    @mock.patch.object(cipd_utils, "get_cipd_version_manifest", autospec=True)
    def test_changelog(self, mock_get_cipd_version_manifest):
        mock_repo = set_up_changelog_mocks(
            mock_get_cipd_version_manifest, "cipd_ver_A", "cipd_ver_B", {
                "repo1":
                    [
                        "rev1_A",
                        "rev1_B",
                        ["[repo1] commit 1", "[repo1] commit 2"],
                        [],
                    ],
                "repo2":
                    [
                        "rev2_A",
                        "rev2_B",
                        [
                            "[repo2] commit 1", "[repo2] commit 2",
                            "[repo2] commit 3"
                        ],
                        [],
                    ]
            })

        changelog = cipd_utils.changelog(
            mock_repo, "package_name", "cipd_ver_A", "cipd_ver_B")
        self.assertEqual(
            changelog,
            textwrap.dedent(
                """\
            -- Changelist --
            repo1:
            [repo1] commit 1
            [repo1] commit 2

            repo2:
            [repo2] commit 1
            [repo2] commit 2
            [repo2] commit 3

            -- Source Revisions --
            repo1: rev1_B
            repo2: rev2_B"""))

    @mock.patch.object(cipd_utils, "get_cipd_version_manifest", autospec=True)
    def test_changelog_new_repo(self, mock_get_cipd_version_manifest):
        mock_repo = set_up_changelog_mocks(
            mock_get_cipd_version_manifest,
            "cipd_ver_A",
            "cipd_ver_B",
            {
                "repo1":
                    [
                        None,  # Repo does not exist in the first version.
                        "rev1_B",
                        ["[repo1] commit 1", "[repo1] commit 2"],
                        [],
                    ],
            })

        changelog = cipd_utils.changelog(
            mock_repo, "package_name", "cipd_ver_A", "cipd_ver_B")
        self.assertEqual(
            changelog,
            textwrap.dedent(
                """\
            -- Changelist --
            repo1:
            [repo1] commit 1
            [repo1] commit 2

            -- Source Revisions --
            repo1: rev1_B"""))

    @mock.patch.object(cipd_utils, "get_cipd_version_manifest", autospec=True)
    def test_changelog_deleted_repo(self, mock_get_cipd_version_manifest):
        mock_repo = set_up_changelog_mocks(
            mock_get_cipd_version_manifest,
            "cipd_ver_A",
            "cipd_ver_B",
            {
                "repo1":
                    [
                        "rev1_A",
                        "rev1_B",
                        ["[repo1] commit 1", "[repo1] commit 2"],
                        [],
                    ],
                "repo2":
                    [
                        "rev2_A",
                        None,  # Repo does not exist in the second version.
                        ["[repo2] this should not be printed"],
                        ["[repo2] this should not be printed either"],
                    ]
            })

        changelog = cipd_utils.changelog(
            mock_repo, "package_name", "cipd_ver_A", "cipd_ver_B")
        self.assertEqual(
            changelog,
            textwrap.dedent(
                """\
            -- Changelist --
            repo1:
            [repo1] commit 1
            [repo1] commit 2

            repo2:
            [removed commits:]
            [repo has been removed]

            -- Source Revisions --
            repo1: rev1_B"""))

    @mock.patch.object(cipd_utils, "get_cipd_version_manifest", autospec=True)
    def test_changelog_removed_commits(self, mock_get_cipd_version_manifest):
        mock_repo = set_up_changelog_mocks(
            mock_get_cipd_version_manifest,
            "cipd_ver_A",
            "cipd_ver_B",
            {
                "repo1":
                    [
                        "rev1_A",
                        "rev1_B",
                        ["[repo1] commit 1", "[repo1] commit 2"],
                        # A is not a direct ancestor of B - it had a commit that
                        # no longer exists in the new version.
                        ["[repo1] removed commit"],
                    ]
            })

        changelog = cipd_utils.changelog(
            mock_repo, "package_name", "cipd_ver_A", "cipd_ver_B")
        self.assertEqual(
            changelog,
            textwrap.dedent(
                """\
            -- Changelist --
            repo1:
            [repo1] commit 1
            [repo1] commit 2
            [removed commits:]
            [repo1] removed commit

            -- Source Revisions --
            repo1: rev1_B"""))


if __name__ == "__main__":
    unittest.main()
