# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(
    "@io_bazel_rules_dart//dart/build_rules/common:context.bzl",
    "collect_dart_context",
    "make_dart_context"
)
load(
    "@io_bazel_rules_dart//dart/build_rules/internal:common.bzl",
    "layout_action",
    "package_spec_action"
)

def compile_kernel_action(context, package_name, dart_exec, kernel_compiler,
                          sdk_root, main, kernel_snapshot_file, manifest_file,
                          main_dilp_file, dilp_list_file):
    """Creates an action that generates the Dart kernel and its dependencies.

    Args:
        context: The rule context.
        package_name: The Dart package name.
        dart_exec: The Dart executable `File`.
        kernel_compiler: The kernel compiler snapshot `File`.
        sdk_root: The Dart SDK root `File` (Dart or Flutter's platform libs).
        main: The main `File`.
        kernel_snapshot_file: The kernel snapshot `File` output.
        manifest_file: The Fuchsia manifest `File` output.
        main_dilp_file: The compiled main dilp `File` output.
        dilp_list_file: The dilplist `File` output.

    Returns:
        Mapping `dict` to be used for packaging.
    """
    build_dir = context.label.name + ".build/"
    dart_ctx = make_dart_context(
        ctx = context,
        package = package_name,
        deps = context.attr.deps)

    # 1. Create the .packages file.
    package_spec_path = context.label.package + "/" + context.label.name + ".packages"
    package_spec = context.new_file(build_dir + package_spec_path)
    package_spec_action(
        ctx = context,
        output = package_spec,
        dart_ctx = dart_ctx,
    )

    # 2. Layout the dependencies into the *.build directory.
    if len(dart_ctx.transitive_srcs.files) > 0:
        build_dir_files = layout_action(
            ctx = context,
            srcs = dart_ctx.transitive_srcs.files,
            output_dir = build_dir,
        )
    else:
        build_dir_files = {}

    # 3. Declare *.dilp files for all dependencies.
    mappings = {}
    dart_ctxs = collect_dart_context(dart_ctx).values()
    for dc in dart_ctxs:
        # There's no need to declare a file for the current package as it
        # is outputed under `main_dilp_file`.
        if dc.package == package_name:
            continue
        dilp_file = context.actions.declare_file(
            context.label.name + "_kernel.dil-" + dc.package + ".dilp")
        mappings['data/' + dc.package + ".dilp"] = dilp_file

    # 4. Compile the kernel.
    context.actions.run(
        executable = dart_exec,
        arguments = [
            "--no-preview-dart-2",
            kernel_compiler.path,
            "--target",
            "dart_runner",
            "--sdk-root",
            sdk_root.dirname,
            "--packages",
            package_spec.path,
            "--manifest",
            manifest_file.path,
            "--output",
            kernel_snapshot_file.path,
            main.path,
        ],
        inputs = build_dir_files.values() + [
            kernel_compiler,
            sdk_root,
            package_spec,
            main,
        ],
        outputs = [
            main_dilp_file,
            dilp_list_file,
            kernel_snapshot_file,
            manifest_file,
        ] + mappings.values(),
        mnemonic = "DartKernelCompiler",
    )
    mappings["data/main.dilp"] = main_dilp_file
    mappings["data/app.dilplist"] = dilp_list_file
    return mappings
