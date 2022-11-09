# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@rules_fuchsia//fuchsia/private/assembly:providers.bzl", "FuchsiaProductAssemblyBundleInfo")

def _platform_aibs_impl(ctx):
    aibs_dir_name = ctx.label.name

    symlinked_aibs = []
    aib_dirs = ctx.files.aib_dirs + [
        aib[FuchsiaProductAssemblyBundleInfo].dir
        for aib in ctx.attr.aibs
    ]

    # Stage all AIB symlinks in one temporary directory so they can be copied
    # over at once.
    # Bazel doesn't allow declaring subpaths of an output dir as outputs, so
    # these symlinks can't be created under `aibs_dir_name` directly.
    tmp_aibs_dir_name = aibs_dir_name + "_tmp"
    for aib_dir in aib_dirs:
        dest = ctx.actions.declare_file(tmp_aibs_dir_name + "/" + aib_dir.basename)
        symlinked_aibs.append(dest)
        ctx.actions.symlink(output = dest, target_file = aib_dir)

    aibs_dir = ctx.actions.declare_directory(aibs_dir_name)
    ctx.actions.run_shell(
        outputs = [aibs_dir],
        inputs = symlinked_aibs,
        command = "ls && mv $1_tmp/* $1",
        arguments = [aibs_dir.path],
    )

    return [
        DefaultInfo(files = depset(direct = [aibs_dir])),
        FuchsiaProductAssemblyBundleInfo(dir = aibs_dir, files = [aibs_dir]),
    ]

platform_aibs = rule(
    doc = """Collect platform AIBs from GN and Bazel into one directory.""",
    implementation = _platform_aibs_impl,
    attrs = {
        "aib_dirs": attr.label_list(
            doc = "a list of paths to AIBs",
            allow_files = True,
            default = [],
        ),
        "aibs": attr.label_list(
            doc = "a list of platform_aib targets",
            providers = [FuchsiaProductAssemblyBundleInfo],
            default = [],
        ),
    },
)
