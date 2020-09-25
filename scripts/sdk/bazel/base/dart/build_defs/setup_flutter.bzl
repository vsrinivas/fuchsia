# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@io_bazel_rules_dart//dart/build_rules/internal:pub.bzl", "pub_repository")

FLUTTER_DOWNLOAD_URL = (
    "https://github.com/flutter/flutter/archive/3d65c21a97a9e9028170db94907f2e5ef7a9ac50.zip"
)

FLUTTER_SHA256 = (
    "49dc899ea80ed0a89843e2dbb617adfb687d3673c07b72cab55d540e34c72a50"
)

# TODO(alainv|fxbug.dev/5224): Pull dependencies automatically from
#     //third_party/dart-pkg/git/flutter/packages/flutter:flutter.
FLUTTER_DEPENDENCIES = {
    "collection": "1.14.11",
    "meta": "1.1.8",
    "typed_data": "1.1.6",
    "vector_math": "2.0.8",
}

def _install_flutter_dependencies():
    """Installs Flutter's dependencies."""
    for name, version in FLUTTER_DEPENDENCIES.items():
        pub_repository(
            name = "vendor_" + name,
            output = ".",
            package = name,
            version = version,
            pub_deps = [],
        )

def _install_flutter_impl(repository_ctx):
    """Installs the flutter repository."""
    # Download Flutter.
    repository_ctx.download_and_extract(
        url = FLUTTER_DOWNLOAD_URL,
        output = ".",
        sha256 = FLUTTER_SHA256,
        type = "zip",
        stripPrefix = "flutter-3d65c21a97a9e9028170db94907f2e5ef7a9ac50",
    )
    # Set up the BUILD file from the Fuchsia SDK.
    repository_ctx.symlink(
        Label("@fuchsia_sdk//build_defs:BUILD.flutter_root"), "BUILD")
    repository_ctx.symlink(
        Label("@fuchsia_sdk//build_defs:BUILD.flutter"), "packages/flutter/BUILD")

_install_flutter = repository_rule(
    implementation = _install_flutter_impl,
)

def setup_flutter():
    _install_flutter_dependencies()
    _install_flutter(name = "vendor_flutter")
