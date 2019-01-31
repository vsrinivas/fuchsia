# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":package_info.bzl", "get_aggregate_info", "PackageGeneratedInfo", "PackageComponentInfo")

"""
Makes a cc_binary ready for inclusion in a fuchsia_package.

Args:
    name: The name of this build target.
    target: The cc_binary or cc_test target to include in the fuchsia package.
    **kwargs: Additional arguments, such as testonly.
"""

def _cc_contents_impl(target, context):
    if not context.rule.kind in ["cc_binary", "cc_test"]:
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

def _cc_binary_component_impl(context):
    if len(context.attr.deps) != 1:
        fail("'deps' attribute must have exactly one element.", "deps")
    return [
        PackageComponentInfo(
            name = context.attr.component_name,
            manifest = context.file.manifest,
        ),
    ]

_cc_binary_component = rule(
    implementation = _cc_binary_component_impl,
    attrs = {
        "deps": attr.label_list(
            doc = "The cc_binary for the component",
            mandatory = True,
            allow_empty = False,
            allow_files = False,
            aspects = [_cc_contents_aspect],
        ),
        "component_name": attr.string(
            doc = "The name of the component",
            mandatory = True,
        ),
        "manifest": attr.label(
            doc = "The component's manifest file (.cmx)",
            mandatory = True,
            allow_single_file = True,
        )
    },
    provides = [PackageComponentInfo],
)

def cc_binary_component(name, deps, component_name, manifest, **kwargs):
    packaged_name = name + "_packaged"

    _cc_binary_component(
        name = packaged_name,
        deps = deps,
        component_name = component_name,
        manifest = manifest,
        **kwargs
    )

    # The filegroup is needed so that the packaging can properly crawl all the
    # dependencies and look for package contents.
    native.filegroup(
        name = name,
        srcs = [
            ":" + packaged_name,
            Label("//build_defs/toolchain:dist"),
            Label("//pkg/fdio"),
            Label("//pkg/sysroot"),
        ],
        **kwargs
    )
