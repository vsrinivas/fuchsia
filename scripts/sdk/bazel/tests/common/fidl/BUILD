# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:fidl_library.bzl", "fidl_library")

package(default_visibility = ["//visibility:public"])

fidl_library(
    name = "simple",
    library = "bazel.examples.simple",
    srcs = [
        "simple.fidl",
    ],
)

fidl_library(
    name = "local_deps",
    library = "bazel.examples.localdeps",
    srcs = [
        "local_deps.fidl",
    ],
    deps = [
        ":simple",
    ],
)

fidl_library(
    name = "fuchsia_deps",
    library = "bazel.examples.fuchsiadeps",
    srcs = [
        "fuchsia_deps.fidl",
    ],
    deps = [
        "@fuchsia_sdk//fidl/fuchsia_sys",
    ],
)
