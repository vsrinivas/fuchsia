# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":dart.bzl", "produce_package_info", "generate_dot_packages_action", "DartLibraryInfo")
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

    package_root = context.actions.declare_directory(
        context.rule.attr.name + "_fidl_dart/lib")

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
        ],
        mnemonic = "FidlGenDart",
    )

    package_name = "fidl_" + library_name.replace(".", "_")
    deps = context.rule.attr.deps + context.attr._deps
    library_info = produce_package_info(package_name, package_root, deps)

    return [
        library_info,
    ]

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
        "_deps": attr.label_list(
            default = [
                Label("//dart/fidl"),
            ],
        ),
    },
    provides = [
        DartLibraryInfo,
    ],
)

def _dart_fidl_library_impl(context):
    info = context.attr.library[DartLibraryInfo]

    # Add all generated directories to force the codegen to happen.
    files = [package.root for package in info.package_map.to_list()]

    # TODO(pylaligand): this should eventually be done in the aspect.
    packages_file = context.actions.declare_file(
        context.attr.name + ".packages")
    generate_dot_packages_action(context, packages_file, info)
    files.append(packages_file)

    return [
        DefaultInfo(files = depset(files)),
        info,
    ]

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
    provides = [
        DartLibraryInfo,
    ],
)
