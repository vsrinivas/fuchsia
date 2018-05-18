#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Defines a package
#
# The package template is used to define a unit of related code and data.
# A package always has a name (defaulting to the target name)
#
# Parameters
#
#   name(string, required)
#       The name of the package
#
#   binary (optional)
#     The path to the the primary binary for the package, relative to
#     $root_build_dir. The binary will be placed in the assembled package at
#     "bin/app" and will be executed by default when running the package.
#
#   deps(list, required)
#       The list of targets to be built into this package
def _fuchsia_package_impl (ctx):
    print("building fuchsia_package %s\n" % ctx.attr.name)
    transitive_runfile_sets = []
    for d in ctx.attr.deps:
        transitive_runfile_sets.append(d.default_runfiles.files)
    transitive_runfiles = depset(transitive = transitive_runfile_sets)

    manifest_file_contents=""
    for runfile in transitive_runfiles.to_list():
        if runfile.extension == '':
            manifest_file_contents += "bin/{}={}\n".format(runfile.basename, runfile.path) 
        elif runfile.extension == 'so':
            manifest_file_contents += "lib/{}={}\n".format(runfile.basename, runfile.path)

    meta_package = ctx.actions.declare_file("meta/package")
    manifest_file_contents += "meta/package={}\n".format(meta_package.path)
    manifest_file = ctx.actions.declare_file("package_manifest")
    ctx.actions.write(output = manifest_file, content = manifest_file_contents)

    package_dir = manifest_file.dirname

    ctx.actions.run(
        outputs = [meta_package],
        executable = ctx.executable.pm,
        arguments = ["-o", package_dir, "-n", ctx.attr.name, "init"],
        mnemonic = "PmInit",
        progress_message = "Initializing Fuchsia PM meta directory {}".format(ctx.attr.name))

    pm_signing_key = ctx.actions.declare_file("development.key")
    ctx.actions.run(
        outputs = [pm_signing_key],
        inputs = [meta_package],
        executable = ctx.executable.pm,
        arguments = ["-o", package_dir, "-k", pm_signing_key.path, "genkey"],
        mnemonic = "PmGenkey",
        progress_message = "Creating Fuchsia PM signing key for {}".format(ctx.attr.name))

    pm_build_inputs = depset(direct = [manifest_file, pm_signing_key, meta_package], transitive = transitive_runfile_sets)
    meta_far = ctx.actions.declare_file("meta.far")
    ctx.actions.run(
        outputs = [meta_far],
        inputs = pm_build_inputs,
        executable = ctx.executable.pm,
        arguments = ["-o", package_dir, "-k", pm_signing_key.path, "-m", manifest_file.path, "build"],
        mnemonic = "PmBuild",
        progress_message = "building Fuchsia package metadata for {}".format(ctx.attr.name))

    pm_archive_inputs = depset(direct = [manifest_file, pm_signing_key, meta_far], transitive = transitive_runfile_sets)
    package_archive = ctx.actions.declare_file("{}-0.far".format(ctx.attr.name))
    ctx.actions.run(
        outputs = [package_archive],
        inputs = pm_archive_inputs,
        executable = ctx.executable.pm,
        arguments = ["-o", package_dir, "-k", pm_signing_key.path, "-m", manifest_file.path, "archive"],
        mnemonic = "PmArchive",
        progress_message = "Creating Fuchsia package archive for {}".format(ctx.attr.name))
    return [DefaultInfo(files = depset([package_archive]))]

fuchsia_package = rule(
    implementation = _fuchsia_package_impl,
    attrs = {
        "deps": attr.label_list(),
        "pm": attr.label(executable=True, cfg="host", allow_files=True)
    },
)


