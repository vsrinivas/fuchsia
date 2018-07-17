# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:dart_app.bzl", "dart_app")
load("@fuchsia_sdk//build_defs:dart_fidl_library.bzl", "dart_fidl_library")
load("@fuchsia_sdk//build_defs:package.bzl", "fuchsia_package")

package(default_visibility = ["//visibility:public"])

dart_fidl_library(
    name = "simple",
    library = "//fidl:simple",
)

dart_fidl_library(
    name = "local_deps",
    library = "//fidl:local_deps",
)

dart_fidl_library(
    name = "fuchsia_deps",
    library = "//fidl:fuchsia_deps",
)

dart_app(
    name = "compile",
    package_name = "tests.fidl_dart_compile",
    main = "instrument_bindings.dart",
    deps = [
        ":simple",
    ],
)

fuchsia_package(
    name = "fidl_dart_package",
    deps = [
        ":compile",
    ],
)
