# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Repository rules used to populate Clang-based repositories."""

load(
    "//:build/bazel/toolchains/clang/clang_utilities.bzl",
    "process_clang_builtins_output",
)
load(
    "//:build/bazel/repository_utils.bzl",
    "get_clang_target_triple",
    "get_fuchsia_host_arch",
    "get_fuchsia_host_os",
    "workspace_root_path",
)

# generate_prebuilt_toolchain_repository() is used to generate
# an external repository that contains a copy of the prebuilt
# Clang toolchain that is already available in the Fuchsia source
# tree, then adding Bazel specific files there.
#
# Because repository_ctx.symlink only works for real files and
# not directories, we have to invoke a special script to perform
# the copy, possibly using hard links.
#
def _generate_prebuilt_toolchain_impl(repo_ctx):
    workspace_dir = str(workspace_root_path(repo_ctx))

    host_os = get_fuchsia_host_os(repo_ctx)
    host_arch = get_fuchsia_host_arch(repo_ctx)

    target_triple = get_clang_target_triple(host_os, host_arch)

    # Copy the content of the clang installation directory into
    # the repository directory, using hard-links whenever possible.
    # This allows us to add Bazel-specific files in this location.

    # Resolve full path of script before executing it, this ensures that the repository
    # rule will be re-run everytime the invoked script is modified.
    script_path = str(repo_ctx.path(Label("@//:build/bazel/scripts/hardlink-directory.py")))

    repo_ctx.execute(
        [
            script_path,
            repo_ctx.attr.clang_install_dir,
            ".",
        ],
        quiet = False,  # False for debugging!
    )

    # Extract the builtin include paths by running the executable once.
    # Only care about C++ include paths. The list for compiling C is actually
    # smaller, but this is not an issue for now.
    #
    # Note that the paths must be absolute for Bazel, which matches the
    # output of the command, so there is not need to change them.
    #
    # Note however that Bazel will use relative paths when creating the list
    # of inputs for C++ compilation actions.

    # Create an empty file to be pre-processed. This is more portable than
    # trying to use /dev/null as the input.
    repo_ctx.file("empty", "")
    command = ["bin/clang", "-E", "-x", "c++", "-v", "./empty"]
    result = repo_ctx.execute(command)

    # Write the result to a file for debugging.
    repo_ctx.file("debug_probe_results.txt", result.stderr)

    short_version, long_version, builtin_include_paths = \
        process_clang_builtins_output(result.stderr)

    # Now convert that into a string that can go into a .bzl file.
    builtin_include_paths_str = "\n".join(["    \"%s\"," % path for path in builtin_include_paths])
    repo_ctx.file("WORKSPACE.bazel", content = "")

    repo_ctx.file("generated_constants.bzl", content = '''
constants = struct(
  host_os = "{host_os}",
  host_arch = "{host_arch}",
  clang_long_version = "{long_version}",
  clang_short_version = "{short_version}",
  target_triple = "{target_triple}",
  builtin_include_paths = [
{builtin_paths}
  ],
)
'''.format(
        host_os = host_os,
        host_arch = host_arch,
        long_version = long_version,
        short_version = short_version,
        target_triple = target_triple,
        builtin_paths = builtin_include_paths_str,
    ))

    repo_ctx.symlink(
        workspace_dir + "/build/bazel/toolchains/clang/prebuilt_clang.BUILD.bazel",
        "BUILD.bazel",
    )

generate_prebuilt_toolchain_repository = repository_rule(
    implementation = _generate_prebuilt_toolchain_impl,
    attrs = {
        "clang_install_dir": attr.string(mandatory = True),
    },
)
