# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:package.bzl", "fuchsia_package")
load("@fuchsia_sdk//build_defs:cc_binary_component.bzl", "cc_binary_component")

load("//build_defs:verify_package.bzl", "verify_package")

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
cc_binary_component(
    name = "packageable",
    deps = [":pkg_dep"],
    component_name = "packageable",
    manifest = "manifest.cmx",
)

# C++ program in a Fuchsia package.
fuchsia_package(
    name = "package",
    deps = [
        ":packageable",
    ],
)

# Verify that the package contains all the expected files.
verify_package(
    name = "package_verify",
    package = ":package",
    files = [
        "bin/pkg_dep",
        "lib/ld.so.1",
        "lib/libshared.so",
        "lib/libsvc.so",
        "meta/packageable.cmx",
    ],
)

# Test the testonly attribute.

cc_test(
    name = "pkg_dep_test",
    srcs = [
        "pkg_dep.cc",
    ],
    deps = [
        ":shared_library",
        "@fuchsia_sdk//pkg/svc",
    ],
    tags = [
        "compile-only"
    ],
)

cc_binary_component(
    name = "packageable_testonly",
    deps = [":pkg_dep_test"],
    component_name = "packageable_testonly",
    manifest = "manifest.cmx",
    testonly = 1,
)

fuchsia_package(
    name = "package_test",
    deps = [
        ":packageable_testonly",
    ],
    testonly = 1,
)

verify_package(
    name = "package_test_verify",
    package = ":package_test",
    files = [
        "bin/pkg_dep_test",
    ],
    testonly = 1,
)
