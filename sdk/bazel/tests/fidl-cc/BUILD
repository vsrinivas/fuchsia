# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:fidl_cc.bzl", "cc_fidl_library")

cc_fidl_library(
    name = "simple",
    library = "//fidl:simple",
)

cc_binary(
    name = "cc-user",
    srcs = [
        "library_user.cc",
    ],
    deps = [
        ":simple",
    ],
)
