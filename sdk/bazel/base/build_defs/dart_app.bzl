# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":dart.bzl", "aggregate_packages", "generate_dot_packages_action", "DartLibraryInfo")
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
    # 1. Create the .packages file.
    packages = context.outputs.packages
    deps = context.attr.deps if hasattr(context.attr, "deps") else []
    info = aggregate_packages(deps)
    generate_dot_packages_action(context, packages, info)
    all_sources = [package.root for package in info.package_map.to_list()]

    # 2. Compile the kernel.
    kernel_snapshot = context.outputs.kernel_snapshot
    manifest = context.outputs.manifest
    main = context.files.main[0]
    context.actions.run(
        executable = context.executable._dart,
        arguments = [
            context.files._kernel_compiler[0].path,
            "--target",
            "dart_runner",
            "--sdk-root",
            context.files._platform_lib[0].dirname,
            "--packages",
            packages.path,
            "--manifest",
            manifest.path,
            "--output",
            kernel_snapshot.path,
            main.path,
        ],
        inputs = all_sources + [
            context.files._kernel_compiler[0],
            context.files._platform_lib[0],
            packages,
            main,
        ],
        outputs = [
            context.outputs.main_dilp,
            context.outputs.dilp_list,
            kernel_snapshot,
            manifest,
        ],
        mnemonic = "DartKernelCompiler",
    )

    # TODO(alainv): Call the Fuchsia package rule.
    dart_jit_runner = context.actions.declare_file("runtime")
    context.actions.write(
        output = dart_jit_runner,
        content = _DART_JIT_RUNNER_CONTENT)
    mappings = {}
    mappings["meta/runtime"] = dart_jit_runner
    mappings["data/main.dilp"] = context.outputs.main_dilp
    mappings["data/app.dilplist"] = context.outputs.dilp_list
    return [
        DefaultInfo(files = depset([kernel_snapshot, manifest])),
        PackageLocalInfo(mappings = mappings.items()),
    ]

dart_app = rule(
    implementation = _dart_app_impl,
    attrs = {
        "main": attr.label(
            doc = "The main script file",
            mandatory = True,
            allow_files = True,
            single_file = True,
        ),
        "deps": attr.label_list(
            doc = "The list of libraries this app depends on",
            mandatory = False,
            providers = [DartLibraryInfo],
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
            default = Label("//tools/dart_prebuilts/dart_runner:platform_strong.dill"),
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
        # The .packages file.
        # TODO(alainv): Maybe remove once dart_library integration is complete.
        "packages": "%{name}.packages",
    },
)
