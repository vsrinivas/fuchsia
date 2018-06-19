# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":package_info.bzl", "PackageLocalInfo")

"""
Makes a cc_binary ready for inclusion in a fuchsia_package.

Parameters

    binary(label, required)
        The cc_binary to package up.
"""

def _packageable_cc_binary(context):
    runfiles = []
    runfiles.append(context.attr.binary[DefaultInfo].default_runfiles.files)
    for dep in context.attr._deps:
        runfiles.append(dep[DefaultInfo].default_runfiles.files)
    runfiles_depset = depset(transitive = runfiles)
    mappings = {}
    for file in runfiles_depset.to_list():
        if file.extension == "":
            mappings["bin/" + file.basename] = file
        elif file.extension == "so":
            mappings["lib/" + file.basename] = file
        else:
            print("Ignoring file: " + file.path)
    return [
        PackageLocalInfo(mappings = mappings.items()),
    ]

packageable_cc_binary = rule(
    implementation = _packageable_cc_binary,
    attrs = {
        "binary": attr.label(
            doc = "The cc_binary to package",
            allow_files = False,
        ),
        "_deps": attr.label_list(
            doc = "The dependencies needed in all packages",
            default = [
                # TODO(pylaligand): this label should be arch-independent.
                Label("//arch/x64/sysroot:dist"),
                Label("//pkg/fdio"),
            ],
        )
    },
)
