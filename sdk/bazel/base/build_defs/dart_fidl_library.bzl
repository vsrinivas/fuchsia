# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(
    "@io_bazel_rules_dart//dart/build_rules/common:context.bzl",
    "make_dart_context"
)
load(
    "@io_bazel_rules_dart//dart/build_rules/internal:analyze.bzl",
    "summary_action",
)
load(":fidl_library.bzl", "FidlLibraryInfo")

# A Dart library backed by a FIDL library.
#
# Parameters
#
#   library
#     Label of the FIDL library.

def _dart_codegen_impl(target, context):
    ir = target[FidlLibraryInfo].ir
    library_name = target[FidlLibraryInfo].name

    package_root_dir = context.rule.attr.name + "_fidl_dart/lib"
    package_root = context.actions.declare_directory(package_root_dir)
    fidl_dart_file = context.new_file(package_root_dir + "/fidl.dart")
    fidl_async_dart_file = context.new_file(
        package_root_dir + "/fidl_async.dart")

    context.actions.run(
        executable = context.executable._fidlgen,
        arguments = [
            "--json",
            ir.path,
            "--output-base",
            package_root.path,
            "--include-base",
            "this_is_a_bogus_value",
        ],
        inputs = [
            ir,
        ],
        outputs = [
            package_root,
            fidl_dart_file,
            fidl_async_dart_file,
        ],
        mnemonic = "FidlGenDart",
    )

    package_name = "fidl_" + library_name.replace(".", "_")
    deps = context.rule.attr.deps + context.attr._deps

    dart_ctx = make_dart_context(
        context,
        generated_srcs = [
            fidl_dart_file,
            fidl_async_dart_file,
        ],
        lib_root = context.label.package + "/" + package_root_dir,
        deps = deps,
        enable_summaries = True,
        package = package_name,
    )

    summary_action(context, dart_ctx)
    files_provider = depset([dart_ctx.strong_summary])

    return struct(
        dart = dart_ctx,
        files_provider = files_provider,
    )

# This aspects runs the FIDL code generator on a given FIDL library.
_dart_codegen = aspect(
    implementation = _dart_codegen_impl,
    attr_aspects = [
        # Propagate the aspect to every dependency of the library.
        "deps",
    ],
    attrs = {
        "_fidlgen": attr.label(
            default = Label("//tools:fidlgen_dart"),
            allow_single_file = True,
            executable = True,
            cfg = "host",
        ),
        "_analyzer": attr.label(
            default = Label("@dart_sdk//:analyzer"),
            executable = True,
            cfg = "host",
        ),
        "_deps": attr.label_list(
            default = [
                Label("//dart/fidl"),
            ],
        ),
    },
)

def _dart_fidl_library_impl(context):
    return struct(
        dart = context.attr.library.dart,
        files = context.attr.library.files_provider,
    )

dart_fidl_library = rule(
    implementation = _dart_fidl_library_impl,
    attrs = {
        "library": attr.label(
            doc = "The FIDL library to generate code for",
            mandatory = True,
            allow_files = False,
            providers = [FidlLibraryInfo],
            aspects = [_dart_codegen],
        ),
    },
)
