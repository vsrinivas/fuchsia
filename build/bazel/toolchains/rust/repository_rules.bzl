# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Repository rules used to populate Rust-based repositories."""

load(
    "//:build/bazel/repository_utils.bzl",
    "workspace_root_path",
)

def _generate_prebuilt_rust_toolchain_repository_impl(repo_ctx):
    repo_ctx.file("WORKSPACE.bazel", content = "")

    workspace_dir = str(workspace_root_path(repo_ctx))

    # Symlink the content of the Rust installation directory into the repository.
    # This allows us to add Bazel-specific files in this location.
    repo_ctx.execute(
        [
            workspace_dir + "/build/bazel/scripts/symlink-directory.py",
            repo_ctx.attr.rust_install_dir,
            ".",
        ],
        quiet = False,  # False for debugging!
    )

    # Symlink the BUILD.bazel file.
    repo_ctx.symlink(
        workspace_dir + "/build/bazel/toolchains/rust/rust.BUILD.bazel",
        "BUILD.bazel",
    )

generate_prebuilt_rust_toolchain_repository = repository_rule(
    implementation = _generate_prebuilt_rust_toolchain_repository_impl,
    attrs = {
        "rust_install_dir": attr.string(
            mandatory = True,
            doc = "Location of prebuilt Rust toolchain installation",
        ),
    },
)
