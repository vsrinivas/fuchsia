# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Sets up the Fuchsia SDK.

Must be called even if all attributes are set to false.
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//build_defs/internal/crosstool:crosstool.bzl", "configure_crosstool")

def fuchsia_setup(with_toolchain=False):
    # Needed for the package component runner tool.
    http_archive(
        name = "subpar",
        url = "https://github.com/google/subpar/archive/1.0.0.zip",
        strip_prefix = "subpar-1.0.0",
    )

    if with_toolchain:
        configure_crosstool(
            name = "fuchsia_crosstool",
        )
