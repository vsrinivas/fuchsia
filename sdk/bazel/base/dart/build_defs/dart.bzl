# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(
    "@io_bazel_rules_dart//dart/build_rules/common:context.bzl",
    "collect_dart_context",
    "make_dart_context",
)
load(
    "@io_bazel_rules_dart//dart/build_rules/internal:common.bzl",
    "package_spec_action",
)

"""Common attributes used by the `compile_kernel_action`."""
COMMON_COMPILE_KERNEL_ACTION_ATTRS = {
    "main": attr.label(
        doc = "The main script file",
        mandatory = True,
        allow_single_file = True,
    ),
    "srcs": attr.label_list(
        doc = "Additional source files",
        allow_files = True,
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
    "space_dart": attr.bool(
        doc = "Whether or not to use SpaceDart (defaults to true)",
        default = True,
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
}

def compile_kernel_action(
        context,
        package_name,
        dest_dir,
        dart_exec,
        kernel_compiler,
        sdk_root,
        main,
        srcs,
        deps,
        kernel_snapshot_file,
        manifest_file,
        main_dilp_file,
        dilp_list_file):
    """Creates an action that generates the Dart kernel and its dependencies.

    Args:
        context: The rule context.
        package_name: The Dart package name.
        dest_dir: The directory under data/ where to install compiled files.
        dart_exec: The Dart executable `File`.
        kernel_compiler: The kernel compiler snapshot `File`.
        sdk_root: The Dart SDK root `File` (Dart or Flutter's platform libs).
        main: The main `File`.
        srcs: Additional list of source `File`.
        deps: A list of `Label`s this app depends on.
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
        deps = deps,
    )
    additional_args = []

    # 1. Create the .packages file.
    package_spec_path = context.label.name + ".packages"
    package_spec = context.actions.declare_file(package_spec_path)
    package_spec_action(
        ctx = context,
        output = package_spec,
        dart_ctx = dart_ctx,
    )

    # 2. Declare *.dilp files for all dependencies.
    data_root = "data/%s/" % dest_dir
    mappings = {}
    dart_ctxs = collect_dart_context(dart_ctx).values()
    for dc in dart_ctxs:
        dilp_file = context.actions.declare_file(
            context.label.name + "_kernel.dil-" + dc.package + ".dilp",
        )
        mappings[data_root + dc.package + ".dilp"] = dilp_file

    # 3. Create a wrapper script around the kernel compiler.
    #    The kernel compiler only generates .dilp files for libraries that are
    #    actually used by app. However, we declare a .dilp file for all packages
    #    in the dependency graph: not creating that file would yield a Bazel error.
    content = "#!/bin/bash\n"
    content += dart_exec.path
    content += " $@ || exit $?\n"
    for dilp in mappings.values():
        content += "if ! [[ -f %s ]]; then\n" % dilp.path
        content += "  echo 'Warning: %s is not needed, generating empty file.' >&2\n" % dilp.path
        content += "  touch %s\n" % dilp.path
        content += "fi\n"

    kernel_script = context.actions.declare_file(context.label.name + "_compile_kernel.sh")
    context.actions.write(
        output = kernel_script,
        content = content,
        is_executable = True,
    )

    # 4. Find all possible roots for multi-root scheme
    roots_dict = {}
    for dc in dart_ctxs:
        dart_srcs = list(dc.dart_srcs)
        if len(dart_srcs) == 0:
            continue
        src = dart_srcs[0]
        index = src.path.find(dc.lib_root)
        if index > 0:
            root = src.path[:index]
            roots_dict[root] = True

    # Include the root for package spec file
    roots_dict[package_spec.root.path] = True

    # And current directory as it was ignored in the previous logic
    roots_dict["."] = True

    for root in roots_dict.keys():
        additional_args += ["--filesystem-root", root]

    if context.attr.space_dart:
        additional_args += ["--gen-bytecode"]

    # 5. Compile the kernel.
    multi_root_scheme = "main-root"
    context.actions.run(
        executable = kernel_script,
        arguments = [
            kernel_compiler.path,
            "--data-dir",
            dest_dir,
            "--target",
            "dart_runner",
            "--platform",
            sdk_root.path,
            "--filesystem-scheme",
            multi_root_scheme,
        ] + additional_args + [
            "--packages",
            "%s:///%s" % (multi_root_scheme, package_spec.short_path),
            "--no-link-platform",
            "--split-output-by-packages",
            "--manifest",
            manifest_file.path,
            "--output",
            kernel_snapshot_file.path,
            "%s:///%s" % (multi_root_scheme, main.short_path),
        ],
        inputs = dart_ctx.transitive_srcs.files + srcs + [
            kernel_compiler,
            sdk_root,
            package_spec,
            main,
            dart_exec,
        ],
        outputs = [
            main_dilp_file,
            dilp_list_file,
            kernel_snapshot_file,
            manifest_file,
        ] + mappings.values(),
        mnemonic = "DartKernelCompiler",
    )
    mappings[data_root + "main.dilp"] = main_dilp_file
    mappings[data_root + "app.dilplist"] = dilp_list_file

    if context.attr.space_dart:
        enable_interpreter = context.actions.declare_file(
            context.label.name + "_enable_interpreter",
        )
        context.actions.write(
            output = enable_interpreter,
            # The existence of this file is enough to enable SpaceDart, we add
            # a random string to prevent the `package` rule from removing this
            # file when empty.
            # See:
            #   https://fuchsia.googlesource.com/topaz/+/2a6073f931edc4136761c5b8dcfd2245efc79d45/runtime/flutter_runner/component.cc#57
            content = "No content",
        )
        mappings["data/enable_interpreter"] = enable_interpreter

    return mappings
