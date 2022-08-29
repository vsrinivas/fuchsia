# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

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
    # The following line does not work, Bazel complains that 'repo_ctx'
    # has no `workspace_root` field or method!?
    # Fixed in 6.0-preXXX, but not in 5.3.0!
    # See https://github.com/bazelbuild/bazel/issues/16042
    #
    # workspace_dir = str(repo_ctx.workspace_root)

    # This is a work-around that based on
    # https://github.com/bazelbuild/bazel/pull/15441
    workspace_dir = str(repo_ctx.path(Label("@//:WORKSPACE.bazel")).dirname)

    # Copy the content of the clang installation directory into
    # the repository directory, using hard-links whenever possible.
    # This allows us to add Bazel-specific files in this location.
    repo_ctx.execute(
        [
            workspace_dir + "/build/bazel/scripts/hardlink-directory.py",
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
    builtin_include_paths = []
    command = ["bin/clang", "-E", "-x", "c++", "-v", "-", "/dev/null"]
    result = repo_ctx.execute(command)
    has_include_paths = False
    has_version = False
    clang_version_prefix = "clang version "
    for line in result.stderr.splitlines():
        if not has_version:
            # Example inputs:
            # Fuchsia clang version 15.0.0 (https://llvm.googlesource.com/a/llvm-project 3a20597776a5d2920e511d81653b4d2b6ca0c855)
            # Debian clang version 14.0.6-2
            pos = line.find(clang_version_prefix)
            if pos >= 0:
                long_version = line[pos + len(clang_version_prefix):].strip()

                # Remove space followed by opening parenthesis.
                pos = long_version.find("(")
                if pos >= 0:
                    long_version = long_version[:pos].rstrip()

                # Remove -suffix
                pos = long_version.find("-")
                if pos >= 0:
                    long_version = long_version[:pos]

                # Split at dots to get the short version.
                short_version, _, _ = long_version.partition(".")
                has_version = True
        if not has_include_paths:
            if line == "#include <...> search starts here:":
                has_include_paths = True
        elif line.startswith(" /"):
            if line.startswith(" /usr/"):
                # ignore lines like /usr/include which should not be used by
                # our build system.
                continue
            builtin_include_paths.append(line[1:])

    # Now convert that into a string that can go into a .bzl file.
    builtin_include_paths_str = "\"" + "\", \"".join(builtin_include_paths) + "\""
    repo_ctx.file("WORKSPACE.bazel", content = "")

    repo_ctx.file("generated_constants.bzl", content = '''
constants = struct(
  clang_long_version = "{long_version}",
  clang_short_version = "{short_version}",
  builtin_include_paths = [{builtin_paths}],
)
'''.format(long_version = long_version, short_version = short_version, builtin_paths = builtin_include_paths_str))

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
