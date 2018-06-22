# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:dart_app.bzl", "dart_app")
load("@fuchsia_sdk//build_defs:fidl_dart.bzl", "dart_fidl_library")

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

# Using a dart_app as this is currently the only way to verify that the
# generated bindings are sound.
# TODO(pylaligand): switch to a better method to test this.
dart_app(
    name = "compile",
    main = "instrument_bindings.dart",
    deps = [
        ":simple",
    ],
    tags = [
        "ignored",
    ],
)
