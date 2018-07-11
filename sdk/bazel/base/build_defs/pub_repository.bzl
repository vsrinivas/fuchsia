# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Inspired by https://github.com/dart-lang/rules_dart/blob/master/dart/build_rules/internal/pub.bzl

_pub_uri = "https://storage.googleapis.com/pub-packages/packages"

def _pub_repository_impl(context):
  package = context.attr.package
  version = context.attr.version

  context.download_and_extract(
      "%s/%s-%s.tar.gz" % (_pub_uri, package, version),
      context.attr.output,
  )

  pub_deps = context.attr.pub_deps
  bazel_deps = ["\"@vendor_%s//:%s\"" % (dep, dep) for dep in pub_deps]
  deps = ",\n".join(bazel_deps)

  context.file("%s/BUILD" % context.attr.output,
"""
load("@fuchsia_sdk//build_defs:dart_library.bzl", "dart_library")

package(default_visibility = ["//visibility:public"])

filegroup(name = "LICENSE_FILES", srcs=["LICENSE"])

dart_library(
    name = "%s",
    package_name = "%s",
    source_dir = "lib",
    deps = [
        %s
    ],
)
""" % (package, package, deps),
  )

pub_repository = repository_rule(
    attrs = {
        "output": attr.string(),
        "package": attr.string(
            mandatory = True,
        ),
        "version": attr.string(
            mandatory = True,
        ),
        "pub_deps": attr.string_list(
            default = [],
        ),
    },
    implementation = _pub_repository_impl,
)
