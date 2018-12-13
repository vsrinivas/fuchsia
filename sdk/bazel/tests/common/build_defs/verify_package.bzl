# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:package_info.bzl", "PackageInfo")

def _verify_package_impl(context):
    # Unpack the package archive.
    archive = context.attr.package[PackageInfo].archive
    archive_dir = context.actions.declare_directory(context.attr.name)
    context.actions.run(
        executable = context.executable._far,
        arguments = [
            "extract",
            "--archive=" + archive.path,
            "--output=" + archive_dir.path,
        ],
        inputs = [
            archive,
        ],
        outputs = [
            archive_dir,
        ],
        mnemonic = "UnpackArchive",
    )

    # Unpack the meta.far archive.
    meta_dir = context.actions.declare_directory(context.attr.name + "_meta")
    context.actions.run(
        executable = context.executable._far,
        arguments = [
            "extract",
            "--archive=" + archive_dir.path + "/meta.far",
            "--output=" + meta_dir.path,
        ],
        inputs = [
            archive_dir,
        ],
        outputs = [
            meta_dir,
        ],
        mnemonic = "UnpackMeta",
    )

    # Read meta/contents and verify that it contains the expected files.
    success_stamp = context.actions.declare_file(context.attr.name + "_success")
    context.actions.run(
        executable = context.executable._verifier,
        arguments = [
            "--meta",
            meta_dir.path,
            "--stamp",
            success_stamp.path,
            "--files",
        ] + context.attr.files,
        inputs = [
            meta_dir,
        ],
        outputs = [
            success_stamp,
        ],
    )
    return [
        DefaultInfo(files = depset([success_stamp])),
    ]

verify_package = rule(
    implementation = _verify_package_impl,
    attrs = {
        "package": attr.label(
            doc = "The label of the package to verify",
            mandatory = True,
            allow_files = False,
            providers = [PackageInfo],
        ),
        "files": attr.string_list(
            doc = "The files expected to exist in the package",
            mandatory = False,
            allow_empty = True,
        ),
        "_far": attr.label(
            default = Label("@fuchsia_sdk//tools:far"),
            allow_single_file = True,
            executable = True,
            cfg = "host",
        ),
        "_verifier": attr.label(
            default = Label("//build_defs:package_verifier"),
            allow_files = True,
            executable = True,
            cfg = "host",
        ),
    },
)
