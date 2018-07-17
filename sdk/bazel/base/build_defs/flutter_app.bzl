# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":dart.bzl", "compile_kernel_action")
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
        dart_exec = context.executable._dart,
        kernel_compiler = context.files._kernel_compiler[0],
        sdk_root = context.files._platform_lib[0],
        main = context.files.main[0],
        kernel_snapshot_file = kernel_snapshot_file,
        manifest_file = manifest_file,
        main_dilp_file = context.outputs.main_dilp,
        dilp_list_file = context.outputs.dilp_list,
    )
    flutter_jit_runner = context.actions.declare_file("runtime")
    context.actions.write(
        output = flutter_jit_runner,
        content = _FLUTTER_JIT_RUNNER_CONTENT)
    mappings["meta/deprecated_runtime"] = flutter_jit_runner
    return [
        DefaultInfo(files = depset([kernel_snapshot_file, manifest_file])),
        PackageLocalInfo(mappings = mappings.items()),
    ]

flutter_app = rule(
    implementation = _flutter_app_impl,
    attrs = {
        "main": attr.label(
            doc = "The main script file",
            mandatory = True,
            allow_files = True,
            single_file = True,
        ),
        "package_name": attr.string(
            doc = "The Dart package name",
            mandatory = True,
        ),
        "deps": attr.label_list(
            doc = "The list of libraries this app depends on",
            mandatory = False,
            providers = ["dart"],
        ),
        "_dart": attr.label(
            default = Label("//tools:dart"),
            allow_single_file = True,
            executable = True,
            cfg = "host",
        ),
        "_kernel_compiler": attr.label(
            default = Label("//tools/dart_prebuilts:kernel_compiler.snapshot"),
            allow_single_file = True,
            cfg = "host",
        ),
        "_platform_lib": attr.label(
            default = Label("//tools/dart_prebuilts/flutter_runner:platform_strong.dill"),
            allow_single_file = True,
            cfg = "host",
        ),
    },
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
