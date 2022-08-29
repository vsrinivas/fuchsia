# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A Bazel module extension is an object that serves as a factory for
# external repositories that cannot be provided by the module registry.
#
# Such an object is created by calling module_extension() and will provide
# functions that are called by MODULE.bazel files to indicate the parameters
# of the repositories they need (e.g. for a Maven dependency, this would
# be its groupId, artifactId and version).
#
# Once module resolution is completed, the implementation function of each
# extension is run, and receive a list of the requests from all modules
# (called "extension tags"), and should use them to create repositories
# using repository rules.
#
# This file contains the module extensions used by the Fuchsia platform build.

# clang_toolchains_extensions:
#
# Generate repositories related to Clang toolchains.
#

load(
    "//:build/bazel/toolchains/clang/repository_rules.bzl",
    "generate_prebuilt_toolchain_repository",
)

def _fuchsia_host_tag(ctx):
    # 'mac os x" -> 'mac'
    # 'windows nt' -> 'windows'
    host = ctx.os.name.split(" ")[0]
    if host == "windows":
        host = "win"
    arch = ctx.os.arch
    if arch == "x86_64" or arch == "amd64":
        arch = "x64"
    elif arch == "aarch64":
        arch = "arm64"
    return host + "-" + arch

def _clang_toolchains_impl(ctx):
    host_tag = _fuchsia_host_tag(ctx)

    for module in ctx.modules:
        for tag in module.tags.prebuilt_toolchain:
            generate_prebuilt_toolchain_repository(
                name = tag.repo_name,
                clang_install_dir = tag.clang_install_root_dir + "/" + host_tag,
            )

clang_toolchains_extension = module_extension(
    implementation = _clang_toolchains_impl,
    tag_classes = {
        "prebuilt_toolchain": tag_class(
            attrs = {
                "repo_name": attr.string(doc = "Repository name", mandatory = True),
                "clang_install_root_dir": attr.string(
                    doc = "Location of prebuilt toolchains, relative to Fuchsia directory.",
                    default = "prebuilt/third_party/clang",
                ),
            },
        ),
    },
)
