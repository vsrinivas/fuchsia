# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":dart.bzl", "COMMON_COMPILE_KERNEL_ACTION_ATTRS", "compile_kernel_action")
load(":package_info.bzl", "PackageLocalInfo")

# A Fuchsia Dart application
#
# Parameters
#
#   main_dart
#     The main script file.
#
#   deps
#     List of libraries to link to this application.

_DART_JIT_RUNNER_CONTENT = """{
    "runner": "dart_jit_runner"
}
"""

def _dart_app_impl(context):
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
    dart_jit_runner = context.actions.declare_file("runtime")
    context.actions.write(
        output = dart_jit_runner,
        content = _DART_JIT_RUNNER_CONTENT,
    )
    mappings["meta/deprecated_runtime"] = dart_jit_runner
    outs = [kernel_snapshot_file, manifest_file]
    return [
        DefaultInfo(files = depset(outs), runfiles = context.runfiles(files = outs)),
        PackageLocalInfo(mappings = mappings.items()),
    ]

dart_app = rule(
    implementation = _dart_app_impl,
    attrs = dict({
        "_platform_lib": attr.label(
            default = Label("//tools/dart_prebuilts/dart_runner:platform_strong.dill"),
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
