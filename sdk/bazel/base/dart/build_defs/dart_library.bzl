# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":dart.bzl", "DartLibraryInfo", "produce_package_info")

def _dart_library_impl(context):
    return [
        produce_package_info(context.attr.package_name,
                             context.files.source_dir[0],
                             context.attr.deps),
    ]

dart_library = rule(
    implementation = _dart_library_impl,
    attrs = {
        "package_name": attr.string(
            doc = "The name of the Dart package",
            mandatory = True,
        ),
        "source_dir": attr.label(
            # TODO(pylaligand): set a default value to "lib".
            doc = "The directory containing the library sources",
            mandatory = True,
            allow_files = True,
            single_file = True,
        ),
        "deps": attr.label_list(
            doc = "The list of libraries this library depends on",
            mandatory = False,
            providers = [DartLibraryInfo],
        ),
    },
    provides = [
        DartLibraryInfo,
    ],
)
