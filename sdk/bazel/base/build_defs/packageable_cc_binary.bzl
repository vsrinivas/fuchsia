# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":package_info.bzl", "get_aggregate_info", "PackageGeneratedInfo", "PackageLocalInfo")

"""
Makes a cc_binary ready for inclusion in a fuchsia_package.

Parameters

    binary(label, required)
        The cc_binary to package up.
"""

def _cc_contents_impl(target, context):
    if context.rule.kind != "cc_binary":
        return [
            PackageGeneratedInfo(mappings = []),
        ]
    mappings = {}
    for file in target[DefaultInfo].files.to_list():
        if file.extension == "":
            mappings["bin/" + file.basename] = file
        elif file.extension == "so":
            mappings["lib/" + file.basename] = file
    return [
        PackageGeneratedInfo(mappings = mappings.items()),
    ]

# This aspect looks for cc_binary targets in the dependency tree of the given
# target. For each of these targets, it then generates package content mappings.
_cc_contents_aspect = aspect(
    implementation = _cc_contents_impl,
    attr_aspects = [
        "data",
        "deps",
        "srcs",
    ],
    provides = [
        PackageGeneratedInfo,
    ],
)

def _packageable_cc_binary_impl(context):
    target_files = context.attr.target[DefaultInfo].files.to_list()
    if len(target_files) != 1:
        fail("Packaged binary should produce a single output", attr="target")
    output = target_files[0]
    if output.extension != "":
        fail("Expected executable, got: " + output.basename, attr="target")
    return [
        # TODO(pylaligand): remove this extra mapping once it's obsolete.
        PackageLocalInfo(mappings = [("bin/app", output)]),
    ]

_packageable_cc_binary = rule(
    implementation = _packageable_cc_binary_impl,
    attrs = {
        "target": attr.label(
            doc = "The cc_binary to package",
            allow_files = False,
            aspects = [
                _cc_contents_aspect,
            ],
        ),
    },
)

def packageable_cc_binary(name, target):
    packaged_name = name + "_packaged"

    _packageable_cc_binary(
        name = packaged_name,
        target = target,
    )

    # The filegroup is needed so that the packaging can properly crawl all the
    # dependencies and look for package contents.
    native.filegroup(
        name = name,
        srcs = [
            ":" + packaged_name,
            # TODO(pylaligand): this label should be arch-independent.
            Label("//arch/x64/sysroot:dist"),
            Label("//pkg/fdio"),
        ]
    )
