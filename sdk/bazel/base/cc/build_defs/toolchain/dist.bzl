# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//build_defs:package_info.bzl", "PackageLocalInfo")

def _toolchain_dist_impl(context):
    mappings = {}
    for file in context.attr.files[DefaultInfo].files.to_list():
        mappings["lib/" + file.basename] = file
    return [
        PackageLocalInfo(mappings = mappings.items()),
    ]

toolchain_dist = rule(
    implementation = _toolchain_dist_impl,
    attrs = {
        "files": attr.label(
            doc = "The filegroup target listing the toolchain libraries to include in packages",
            mandatory = True,
            allow_files = False,
        ),
    },
)
