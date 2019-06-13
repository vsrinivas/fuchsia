# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":package_info.bzl", "PackageAggregateInfo", "PackageComponentInfo",
     "PackageGeneratedInfo", "PackageInfo", "PackageLocalInfo",
     "get_aggregate_info")

"""
Defines a Fuchsia package

The package template is used to define a unit of related code and data.

Parameters

    name(string, required)
        The name of the package

    deps(list, required)
        The list of targets to be built into this package
"""

# The attributes along which the aspect propagates.
_ASPECT_ATTRIBUTES = [
    "data",
    "deps",
    "srcs",
]

def _info_impl(target, context):
    components = []
    mappings = []
    if PackageComponentInfo in target:
        info = target[PackageComponentInfo]
        components += [(info.name, info.manifest)]
    if PackageLocalInfo in target:
        mappings += target[PackageLocalInfo].mappings
    if PackageGeneratedInfo in target:
        mappings += target[PackageGeneratedInfo].mappings
    deps = []
    for attribute in _ASPECT_ATTRIBUTES:
        if hasattr(context.rule.attr, attribute):
            value = getattr(context.rule.attr, attribute)
            deps += value
    return [
        get_aggregate_info(components, mappings, deps),
    ]

# An aspect which turns PackageLocalInfo providers into a PackageAggregateInfo
# provider to identify all elements which need to be included in the package.
_info_aspect = aspect(
    implementation = _info_impl,
    attr_aspects = _ASPECT_ATTRIBUTES,
    provides = [
        PackageAggregateInfo,
    ],
    # If any other aspect is applied to produce package mappings, let the result
    # of that process be visible to the present aspect.
    required_aspect_providers = [
        PackageGeneratedInfo,
    ],
)

def _fuchsia_package_impl(context):
    # List all the files that need to be included in the package.
    info = get_aggregate_info([], [], context.attr.deps)
    manifest_file_contents = ""
    package_contents = []

    # Generate the manifest file with a script: this helps ignore empty files.
    base = context.attr.name + "_pkg/"
    manifest_file = context.actions.declare_file(base + "package_manifest")

    content = "#!/bin/bash\n"
    for dest, source in info.mappings.to_list():
        # Only add file to the manifest if not empty.
        content += "if [[ -s %s ]]; then\n" % source.path
        content += "  echo '%s=%s' >> %s\n" % (dest, source.path,
                                               manifest_file.path)
        content += "fi\n"
        package_contents.append(source)

    # Add cmx file for each component.
    for name, cmx in info.components.to_list():
        content += "echo 'meta/%s.cmx=%s' >> %s\n" % (name, cmx.path,
                                                      manifest_file.path)
        package_contents.append(cmx)

    # Add the meta/package file to the manifest.
    meta_package = context.actions.declare_file(base + "meta/package")
    content += "echo 'meta/package=%s' >> %s\n" % (meta_package.path,
                                                   manifest_file.path)

    # Write the manifest file.
    manifest_script = context.actions.declare_file(base + "package_manifest.sh")
    context.actions.write(
        output = manifest_script,
        content = content,
        is_executable = True,
    )
    context.actions.run(
        executable = manifest_script,
        inputs = package_contents,
        outputs = [
            manifest_file,
        ],
        mnemonic = "FuchsiaManifest",
    )

    # Initialize the package's meta directory.
    package_dir = manifest_file.dirname
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

    # Build the package metadata.
    meta_far = context.actions.declare_file(base + "meta.far")
    context.actions.run(
        executable = context.executable._pm,
        arguments = [
            "-o",
            package_dir,
            "-m",
            manifest_file.path,
            "build",
        ],
        inputs = package_contents + [
            manifest_file,
            meta_package,
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
            "-m",
            manifest_file.path,
            "archive",
        ],
        inputs = [
            manifest_file,
            meta_far,
        ] + package_contents,
        outputs = [
            package_archive,
        ],
        mnemonic = "PmArchive",
    )

    components_file = context.actions.declare_file(context.attr.name + "_components.txt")
    components_contents = "\n".join([n for n, _ in info.components.to_list()])
    context.actions.write(
        output = components_file,
        content = components_contents,
    )

    executable_file = context.actions.declare_file(context.attr.name + "_run.sh")
    executable_contents = """#!/bin/sh\n
%s \\
    --config %s \\
    --package-name %s \\
    --package %s \\
    --dev-finder %s \\
    --pm %s \\
    run \\
    \"$@\"
""" % (
        context.executable._runner.short_path,
        components_file.short_path,
        context.attr.name,
        package_archive.short_path,
        context.executable._dev_finder.short_path,
        context.executable._pm.short_path,
    )
    context.actions.write(
        output = executable_file,
        content = executable_contents,
        is_executable = True,
    )

    runfiles = context.runfiles(files = [
        components_file,
        context.executable._dev_finder,
        context.executable._pm,
        context.executable._runner,
        executable_file,
        package_archive,
    ])

    return [
        DefaultInfo(
            files = depset([package_archive]),
            executable = executable_file,
            runfiles = runfiles,
        ),
        PackageInfo(
            name = context.attr.name,
            archive = package_archive,
        ),
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
        "_dev_finder": attr.label(
            default = Label("//tools:dev_finder"),
            allow_single_file = True,
            executable = True,
            cfg = "host",
        ),
        "_runner": attr.label(
            default = Label("//build_defs/internal/component_runner:component_runner.par"),
            allow_single_file = True,
            executable = True,
            cfg = "host",
        ),
    },
    provides = [PackageInfo],
    executable = True,
)
