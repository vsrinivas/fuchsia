# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:fidl_cc.bzl", "cc_fidl_library")

cc_fidl_library(
    name = "simple",
    library = "//fidl:simple",
)

cc_binary(
    name = "simple-user",
    srcs = [
        "simple_user.cc",
    ],
    deps = [
        ":simple",
    ],
)

cc_fidl_library(
    name = "local_deps",
    library = "//fidl:local_deps",
    # Currently failing as C++ bindings do not handle dependencies between FIDL
    # libraries yet. This is blocked on the availability of C++ compilation
    # support in Skylark (coming soon).
    tags = [
        "ignored",
    ],
)
