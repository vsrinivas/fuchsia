# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:package.bzl", "fuchsia_package")
load("@fuchsia_sdk//build_defs:packageable_cc_binary.bzl", "packageable_cc_binary")

# Vanilla C++ program.
cc_binary(
    name = "compilation",
    srcs = [
        "compilation.cc",
    ],
)

# Local shared library for packaging.
cc_binary(
    name = "libshared.so",
    srcs = [
        "library.cc",
        "library.h",
    ],
    linkshared = True,
)

cc_library(
    name = "shared_library",
    hdrs = [
        "library.h",
    ],
    srcs = [
        ":libshared.so",
    ],
    includes = [
        ".",
    ],
)

# C++ program with dependency on a Fuchsia library.
cc_binary(
    name = "pkg_dep",
    srcs = [
        "pkg_dep.cc",
    ],
    deps = [
        ":shared_library",
        "@fuchsia_sdk//pkg/svc",
    ],
)

# Prepare the binary for inclusion in a package.
packageable_cc_binary(
    name = "packageable",
    target = ":pkg_dep",
)

# C++ program in a Fuchsia package.
fuchsia_package(
    name = "package",
    deps = [
        ":packageable",
    ],
)
