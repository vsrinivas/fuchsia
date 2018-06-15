# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:fidl.bzl", "fidl_library")

package(default_visibility = ["//visibility:public"])

fidl_library(
    name = "fidl",
    library = "bazel.examples.helloworld",
    srcs = [
        "library.fidl",
    ],
)

fidl_library(
    name = "dependencies",
    library = "bazel.examples.dependencies",
    srcs = [
        "dependencies.fidl",
    ],
    deps = [
        ":fidl",
    ],
)
