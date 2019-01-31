# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A FIDL library.
#
# Parameters
#
#   library
#     Name of the FIDL library.
#
#   srcs
#     List of source files.
#
#   deps
#     List of labels for FIDL libraries this library depends on.

FidlLibraryInfo = provider(
    fields = {
        # TODO(pylaligand): this should be a depset.
        "info": "List of structs(name, files) representing the library's dependencies",
        "name": "Name of the FIDL library",
        "ir": "Path to the JSON file with the library's intermediate representation",
    },
)

def _gather_dependencies(deps):
    info = []
    libs_added = []
    for dep in deps:
        for lib in dep[FidlLibraryInfo].info:
            name = lib.name
            if name in libs_added:
                continue
            libs_added.append(name)
            info.append(lib)
    return info

def _fidl_library_impl(context):
    ir = context.outputs.ir
    tables = context.outputs.coding_tables
    library_name = context.attr.library

    info = _gather_dependencies(context.attr.deps)
    info.append(struct(
        name = library_name,
        files = context.files.srcs,
    ))

    files_argument = []
    inputs = []
    for lib in info:
        files_argument += ["--files"] + [f.path for f in lib.files]
        inputs.extend(lib.files)

    context.actions.run(
        executable = context.executable._fidlc,
        arguments = [
            "--json",
            ir.path,
            "--name",
            library_name,
            "--tables",
            tables.path,
        ] + files_argument,
        inputs = inputs,
        outputs = [
            ir,
            tables,
        ],
        mnemonic = "Fidlc",
    )

    return [
        # Exposing the coding tables here so that the target can be consumed as a
        # C++ source.
        DefaultInfo(files = depset([tables])),
        # Passing library info for dependent libraries.
        FidlLibraryInfo(info=info, name=library_name, ir=ir),
    ]

fidl_library = rule(
    implementation = _fidl_library_impl,
    attrs = {
        "library": attr.string(
            doc = "The name of the FIDL library",
            mandatory = True,
        ),
        "srcs": attr.label_list(
            doc = "The list of .fidl source files",
            mandatory = True,
            allow_files = True,
            allow_empty = False,
        ),
        "deps": attr.label_list(
            doc = "The list of libraries this library depends on",
            mandatory = False,
            providers = [FidlLibraryInfo],
        ),
        "_fidlc": attr.label(
            default = Label("//tools:fidlc"),
            allow_single_file = True,
            executable = True,
            cfg = "host",
        ),
    },
    outputs = {
        # The intermediate representation of the library, to be consumed by bindings
        # generators.
        "ir": "%{name}_ir.json",
        # The C coding tables.
        "coding_tables": "%{name}_tables.cc",
    },
)
