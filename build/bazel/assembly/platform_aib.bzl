# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@rules_fuchsia//fuchsia/private:providers.bzl", "FuchsiaPackageInfo")
load("@rules_fuchsia//fuchsia/private/assembly:providers.bzl", "FuchsiaProductAssemblyBundleInfo")

"""Rule for generating an assembly input bundle."""

def _create_pkg_list_file(ctx, pkgs, set_name):
    pkg_list_file = ctx.actions.declare_file("%s_%s_pkg_list.json" % (ctx.label.name, set_name))
    pkg_manifest_paths = [dep[FuchsiaPackageInfo].package_manifest.path for dep in pkgs]
    ctx.actions.write(
        pkg_list_file,
        json.encode_indent(pkg_manifest_paths, indent = "  "),
    )
    return pkg_list_file

def _platform_aib_impl(ctx):
    base_pkg_list_file = _create_pkg_list_file(ctx, ctx.attr.base_packages, "base")
    cache_pkg_list_file = _create_pkg_list_file(ctx, ctx.attr.cache_packages, "cache")
    drivers_pkg_list_file = _create_pkg_list_file(ctx, ctx.attr.base_driver_packages, "drivers")
    bootfs_pkg_list_file = _create_pkg_list_file(ctx, ctx.attr.bootfs_packages, "bootfs")

    inputs = [base_pkg_list_file, cache_pkg_list_file, drivers_pkg_list_file, bootfs_pkg_list_file]
    all_pkgs = ctx.attr.base_packages + ctx.attr.cache_packages + ctx.attr.base_driver_packages + ctx.attr.bootfs_packages
    inputs += [f for pkg in all_pkgs for f in pkg[FuchsiaPackageInfo].files]
    out_dir = ctx.actions.declare_directory(ctx.label.name)
    ctx.actions.run(
        outputs = [out_dir],
        inputs = inputs,
        executable = ctx.executable._aib_tool,
        arguments = [
            "create",
            "--outdir",
            out_dir.path,
            "--base-pkg-list",
            base_pkg_list_file.path,
            "--cache-pkg-list",
            cache_pkg_list_file.path,
            "--drivers-pkg-list",
            drivers_pkg_list_file.path,
            "--bootfs-pkg-list",
            bootfs_pkg_list_file.path,
        ],
    )

    return [
        DefaultInfo(files = depset(direct = [out_dir])),
        FuchsiaProductAssemblyBundleInfo(dir = out_dir),
    ]

platform_aib = rule(
    doc = """Creates a platform assembly input bundle archive.""",
    implementation = _platform_aib_impl,
    provides = [FuchsiaProductAssemblyBundleInfo],
    attrs = {
        "base_packages": attr.label_list(
            doc = "Fuchsia packages to be included in base of this AIB",
            providers = [FuchsiaPackageInfo],
            default = [],
        ),
        "cache_packages": attr.label_list(
            doc = "Fuchsia packages to be included in cache of this AIB",
            providers = [FuchsiaPackageInfo],
            default = [],
        ),
        "base_driver_packages": attr.label_list(
            doc = "Fuchsia driver packages to be included in base of this AIB",
            providers = [FuchsiaPackageInfo],
            default = [],
        ),
        "bootfs_packages": attr.label_list(
            doc = "Fuchsia packages to be included in bootfs of this AIB",
            providers = [FuchsiaPackageInfo],
            default = [],
        ),
        "_aib_tool": attr.label(
            default = "//build/assembly/scripts:assembly_input_bundle_tool",
            executable = True,
            cfg = "exec",
        ),
    },
)
