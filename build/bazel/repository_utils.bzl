# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Common utilities for repository rules."""

def workspace_root_path(repo_ctx):
    """Return the main workspace repository directory.

    Args:
      repo_ctx: repository context
    Returns:
      A Path object for the workspace root.
    """

    # Starting with Bazel 6.0, repo_ctx.workspace_root can be used.
    # The rest if a work-around that based on
    # https://github.com/bazelbuild/bazel/pull/15441
    bazel_version_major = native.bazel_version.split(".")[0]
    if int(bazel_version_major) >= 6:
        return repo_ctx.workspace_root
    else:
        return repo_ctx.path(Label("@//:WORKSPACE.bazel")).dirname
