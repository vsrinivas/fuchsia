# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Sets up the Fuchsia SDK.

Must be called even if all attributes are set to false.
"""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("//build_defs/internal/crosstool:crosstool.bzl", "configure_crosstool")

def fuchsia_setup(with_toolchain = False):
    # Needed for the package component runner tool.
    git_repository(
        name = "subpar",
        tag = "2.0.0",
        remote = "https://fuchsia.googlesource.com/third_party/github.com/google/subpar.git",
    )

    if with_toolchain:
        configure_crosstool(
            name = "fuchsia_crosstool",
        )
