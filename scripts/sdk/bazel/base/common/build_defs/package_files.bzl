# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":package_info.bzl", "PackageLocalInfo")

"""
Declares some files to be included in a Fuchsia package

Parameters

    name(string, required)
        The name of the targets

    contents(dict, required)
        The mappings of source file to path in package
"""

def _package_files_impl(context):
    mappings = {}
    for label, dest in context.attr.contents.items():
        source = label.files.to_list()[0]
        mappings[dest] = source
    return [
        PackageLocalInfo(mappings = mappings.items()),
    ]

package_files = rule(
    implementation = _package_files_impl,
    attrs = {
        "contents": attr.label_keyed_string_dict(
            doc = "Mappings of source file to path in package",
            mandatory = True,
            allow_files = True,
        )
    }
)
