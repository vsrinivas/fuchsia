# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":dart.bzl", "COMMON_COMPILE_KERNEL_ACTION_ATTRS", "compile_kernel_action")
load(":package_info.bzl", "PackageLocalInfo")

# A Fuchsia Flutter application
#
# Parameters
#
#   main_dart
#     The main script file.
#
#   deps
#     List of libraries to link to this application.

_FLUTTER_JIT_RUNNER_CONTENT = """{
    "runner": "flutter_jit_runner"
}
"""

def _flutter_app_impl(context):
    kernel_snapshot_file = context.outputs.kernel_snapshot
    manifest_file = context.outputs.manifest
    mappings = compile_kernel_action(
        context = context,
        package_name = context.attr.package_name,
        fuchsia_package_name = context.attr.fuchsia_package_name,
        dart_exec = context.executable._dart,
        kernel_compiler = context.files._kernel_compiler[0],
        sdk_root = context.files._platform_lib[0],
        main = context.files.main[0],
        srcs = context.files.srcs,
        kernel_snapshot_file = kernel_snapshot_file,
        manifest_file = manifest_file,
        main_dilp_file = context.outputs.main_dilp,
        dilp_list_file = context.outputs.dilp_list,
    )
    flutter_jit_runner = context.actions.declare_file("runtime")
    context.actions.write(
        output = flutter_jit_runner,
        content = _FLUTTER_JIT_RUNNER_CONTENT,
    )
    mappings["meta/deprecated_runtime"] = flutter_jit_runner

    # Package the assets.
    data_root = "data/%s/" % context.attr.fuchsia_package_name
    asset_manifest_dict = {}
    package_name_len = len(context.label.package)
    for asset in context.files.assets:
        # Remove the package name from the path.
        short_path = asset.short_path[package_name_len + 1:]

        # TODO(alainv): Remove once duplication is no longer needed from
        #               https://github.com/flutter/flutter/pull/20728
        mappings["data/%s" % short_path] = asset
        mappings[data_root + short_path] = asset
        asset_manifest_dict[short_path] = [short_path]

    asset_manifest = context.actions.declare_file("AssetManifest.json")
    context.actions.write(
        output = asset_manifest,
        content = "%s" % asset_manifest_dict,
    )

    # TODO(alainv): Remove once duplication is no longer needed from
    #               https://github.com/flutter/flutter/pull/20728
    mappings["data/AssetManifest.json"] = asset_manifest
    mappings[data_root + "AssetManifest.json"] = asset_manifest
    outs = [kernel_snapshot_file, manifest_file]
    return [
        DefaultInfo(files = depset(outs), runfiles = context.runfiles(files = outs)),
        PackageLocalInfo(mappings = mappings.items()),
    ]

flutter_app = rule(
    implementation = _flutter_app_impl,
    attrs = dict({
        "assets": attr.label_list(
            doc = "The app's assets",
            allow_files = True,
        ),
        "_platform_lib": attr.label(
            default = Label("//tools/dart_prebuilts/flutter_runner:platform_strong.dill"),
            allow_single_file = True,
            cfg = "host",
        ),
    }.items() + COMMON_COMPILE_KERNEL_ACTION_ATTRS.items()),
    outputs = {
        # Kernel snapshot.
        "kernel_snapshot": "%{name}_kernel.dil",
        # Main dilp file.
        "main_dilp": "%{name}_kernel.dil-main.dilp",
        # Dilp list.
        "dilp_list": "%{name}_kernel.dilpmanifest.dilplist",
        # Fuchsia package manifest file.
        "manifest": "%{name}_kernel.dilpmanifest",
    },
)
