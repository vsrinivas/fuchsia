# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:flutter_app.bzl", "flutter_app")
load("@fuchsia_sdk//build_defs:package.bzl", "fuchsia_package")

flutter_app(
    name = "app",
    main = "main.dart",
    assets = [
        "assets/logo.png",
    ],
    package_name = "tests.flutter_app",
    deps = [
        "@vendor_flutter//:flutter",
    ],
)

fuchsia_package(
    name = "package",
    deps = [
        ":app",
    ],
)
