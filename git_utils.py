# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import os.path
import subprocess


def _get_diff_base():
    """Returns the newest local commit that is also in the upstream branch, or
    "HEAD" if there is no upstream branch.
    """
    try:
        with open(os.devnull, 'w') as devnull:
            upstream = subprocess.check_output([
                "git", "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"
            ], stderr = devnull).strip()
            # Get local commits not in upstream.
            local_commits = filter(
                len,
                subprocess.check_output(
                    ["git", "rev-list", "HEAD", "^" + upstream, "--"]).split("\n"))
            if not local_commits:
                return "HEAD"

            # Return parent of the oldest commit.
            return subprocess.check_output(
                ["git", "rev-parse", local_commits[-1] + "^"],
                stderr = devnull).strip()

    except subprocess.CalledProcessError:
        return "HEAD"


def get_git_root():
    """Returns the path of the root of the git repository."""
    return subprocess.check_output(["git", "rev-parse",
                                    "--show-toplevel"]).strip()


def get_diff_files():
    """Returns absolute paths to files that are locally modified, staged or
    touched by any commits introduced on the local branch.
    """

    list_command = [
        "git", "diff-index", "--name-only",
        _get_diff_base()
    ]
    git_root_path = get_git_root()
    paths = filter(len, subprocess.check_output(list_command).split("\n"))
    return [ os.path.join(git_root_path, x) for x in paths ]

def get_all_files():
    """Returns absolute paths to all files in the git repo under the current
    working directory.
    """
    list_command = ["git", "ls-files"]
    paths = filter(len, subprocess.check_output(list_command).split("\n"))
    return [ os.path.abspath(x) for x in paths ]
