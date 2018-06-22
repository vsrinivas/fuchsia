# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":package_info.bzl", "get_aggregate_info", "PackageAggregateInfo", "PackageLocalInfo")

"""
Defines a Fuchsia package

The package template is used to define a unit of related code and data.

Parameters

    name(string, required)
        The name of the package

    deps(list, required)
        The list of targets to be built into this package
"""

def _info_impl(target, context):
    mappings = []
    if PackageLocalInfo in target:
        mappings = target[PackageLocalInfo].mappings
    deps = context.attr.deps if hasattr(context.attr, "deps") else []
    return [get_aggregate_info(mappings, deps)]

# An aspect which turns PackageLocalInfo providers into a PackageAggregateInfo
# provider to identify all elements which need to be included in the package.
_info_aspect = aspect(
    implementation = _info_impl,
    attr_aspects = [
        "deps",
    ],
    provides = [
        PackageAggregateInfo,
    ],
)

def _fuchsia_package_impl(context):
    # List all the files that need to be included in the package.
    info = get_aggregate_info([], context.attr.deps)
    manifest_file_contents = ""
    package_contents = []
    for mapping in info.contents.to_list():
        manifest_file_contents += "%s=%s\n" % (mapping[0], mapping[1].path)
        package_contents.append(mapping[1])

    base = context.attr.name + "_pkg/"
    meta_package = context.actions.declare_file(base + "meta/package")
    manifest_file_contents += "meta/package=%s\n" % meta_package.path

    manifest_file = context.actions.declare_file(base + "package_manifest")
    package_dir = manifest_file.dirname

    # Write the manifest file.
    context.actions.write(
        output = manifest_file,
        content = manifest_file_contents,
    )

    # Initialize the package's meta directory.
    context.actions.run(
        executable = context.executable._pm,
        arguments = [
            "-o",
            package_dir,
            "-n",
            context.attr.name,
            "init",
        ],
        outputs = [
            meta_package,
        ],
        mnemonic = "PmInit",
    )

    # TODO(pylaligand): figure out how to specify this key.
    # Generate a signing key.
    signing_key = context.actions.declare_file(base + "development.key")
    context.actions.run(
        executable = context.executable._pm,
        arguments = [
            "-o",
            package_dir,
            "-k",
            signing_key.path,
            "genkey",
        ],
        inputs = [
            meta_package,
        ],
        outputs = [
            signing_key,
        ],
        mnemonic = "PmGenkey",
    )

    # Build the package metadata.
    meta_far = context.actions.declare_file(base + "meta.far")
    context.actions.run(
        executable = context.executable._pm,
        arguments = [
            "-o",
            package_dir,
            "-k",
            signing_key.path,
            "-m",
            manifest_file.path,
            "build",
        ],
        inputs = package_contents + [
            manifest_file,
            meta_package,
            signing_key,
        ],
        outputs = [
            meta_far,
        ],
        mnemonic = "PmBuild",
    )

    # Create the package archive.
    package_archive = context.actions.declare_file(base + context.attr.name + "-0.far")
    context.actions.run(
        executable = context.executable._pm,
        arguments = [
            "-o",
            package_dir,
            "-k",
            signing_key.path,
            "-m",
            manifest_file.path,
            "archive",
        ],
        inputs = [
            manifest_file,
            signing_key,
            meta_far,
        ] + package_contents,
        outputs = [
            package_archive,
        ],
        mnemonic = "PmArchive",
    )

    return [
        DefaultInfo(files = depset([package_archive])),
    ]

fuchsia_package = rule(
    implementation = _fuchsia_package_impl,
    attrs = {
        "deps": attr.label_list(
            doc = "The objects to include in the package",
            aspects = [
                _info_aspect,
            ],
            mandatory = True,
        ),
        "_pm": attr.label(
            default = Label("//tools:pm"),
            allow_single_file = True,
            executable = True,
            cfg = "host",
        ),
    },
)
