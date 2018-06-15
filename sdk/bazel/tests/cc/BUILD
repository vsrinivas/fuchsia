# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Vanilla C++ program.
cc_binary(
    name = "compilation",
    srcs = [
        "compilation.cc",
    ],
)

# C++ program with dependency on a Fuchsia library.
cc_binary(
    name = "pkg_dep",
    srcs = [
        "pkg_dep.cc",
    ],
    deps = [
        "@fuchsia_sdk//pkg/fdio",
    ],
)
