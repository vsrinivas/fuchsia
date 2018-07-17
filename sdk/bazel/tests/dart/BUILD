# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:dart_app.bzl", "dart_app")
load("@fuchsia_sdk//build_defs:package.bzl", "fuchsia_package")

dart_app(
    name = "dart",
    package_name = "tests.dart_app",
    main = "hello_world.dart",
)

fuchsia_package(
    name = "hello_dart_package",
    deps = [
        ":dart",
    ],
)
