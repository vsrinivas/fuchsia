# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
C++ toolchain definitions for Clang.
"""

load("@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl", "tool_path")
load("@prebuilt_clang//:generated_constants.bzl", clang_constants = "constants")

def _prebuilt_clang_cc_toolchain_config_impl(ctx):
    clang_binprefix = "bin/"

    # See CppConfiguration.java class in Bazel sources for the list of
    # all tool_path() names that must be defined.
    tool_paths = [
        tool_path(name = "ar", path = clang_binprefix + "llvm-ar"),
        tool_path(name = "cpp", path = clang_binprefix + "cpp"),
        tool_path(name = "gcc", path = clang_binprefix + "clang"),
        tool_path(name = "gcov", path = "/usr/bin/false"),
        tool_path(name = "gcov-tool", path = "/usr/bin/false"),
        tool_path(name = "ld", path = clang_binprefix + "llvm-ld"),
        tool_path(name = "llvm-cov", path = clang_binprefix + "llvm-cov"),
        tool_path(name = "nm", path = clang_binprefix + "llvm-nm"),
        tool_path("objcopy", path = clang_binprefix + "llvm-objcopy"),
        tool_path("objdump", path = clang_binprefix + "llvm-objdump"),
        tool_path("strip", path = clang_binprefix + "llvm-strip"),
        tool_path(name = "dwp", path = "/usr/bin/false"),
        tool_path(name = "llbm-profdata", path = clang_binprefix + "llvm-profdata"),
    ]

    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        toolchain_identifier = "prebuilt_clang",
        tool_paths = tool_paths,
        features = [],
        action_configs = [],
        cxx_builtin_include_directories = clang_constants.builtin_include_paths,
        builtin_sysroot = ctx.attr.sysroot,
        target_cpu = "_".join([ctx.attr.target_os, ctx.attr.target_arch]),

        # Required by constructor, but otherwise ignored by Bazel.
        # These string values are arbitrary, but are easy to grep
        # in our source tree if they ever happen to appear in
        # build error messages.
        host_system_name = "__bazel_host_system_name__",
        target_system_name = "__bazel_target_system_name__",
        target_libc = "__bazel_target_libc__",
        abi_version = "__bazel_abi_version__",
        abi_libc_version = "__bazel_abi_libc_version__",
        compiler = "__bazel_compiler__",
    )

_prebuilt_clang_cc_toolchain_config = rule(
    implementation = _prebuilt_clang_cc_toolchain_config_impl,
    attrs = {
        "target_os": attr.string(mandatory = True),
        "target_arch": attr.string(mandatory = True),
        "sysroot": attr.string(mandatory = True),
    },
)

def prebuilt_clang_cc_toolchain(
        name,
        host_os,
        host_arch,
        target_os,
        target_arch,
        sysroot_files,
        sysroot_path):
    """Define C++ toolchain related targets for a prebuilt Clang installation.

    This defines cc_toolchain(), cc_toolchain_config() and toolchain() targets
    for a given Clang prebuilt installation and target (os,arch) pair
    with a specific sysroot.

    Args:
       name: Name of the cc_toolchain() target. The corresponding
         toolchain() target will use "${name}_cc_toolchain", and
         the cc_toolchain_config() will use "${name}_toolchain_config".

       host_os: Host os string (e.g. "linux" or "mac").
       host_arch: Host cpu architecture string (e.g. "x64" or "arm64").
       target_os: Target os string (e.g. "linux" or "fuchsia").
       target_arch: Target cpu architecture string.
       sysroot_files: A target list for the sysroot files to be used.
       sysroot_path: Path to the sysroot directory to be used.
    """
    empty = "@prebuilt_clang//:empty"

    _prebuilt_clang_cc_toolchain_config(
        name = name + "_toolchain_config",
        target_os = target_os,
        target_arch = target_arch,
        sysroot = sysroot_path,
    )

    common_compiler_files = ["@prebuilt_clang//:compiler_binaries"]
    common_linker_files = ["@prebuilt_clang//:linker_binaries"]

    native.filegroup(
        name = name + "_compiler_files",
        srcs = common_compiler_files + sysroot_files,
    )

    native.filegroup(
        name = name + "_linker_files",
        srcs = common_linker_files + sysroot_files,
    )

    native.filegroup(
        name = name + "_all_files",
        srcs = common_compiler_files + common_linker_files + sysroot_files,
    )

    native.cc_toolchain(
        name = name,
        all_files = ":%s_all_files" % name,
        ar_files = "@prebuilt_clang//:ar_binaries",
        as_files = empty,
        compiler_files = ":%s_compiler_files" % name,
        dwp_files = empty,
        linker_files = ":%s_linker_files" % name,
        objcopy_files = "@prebuilt_clang//:objcopy_binaries",
        strip_files = "@prebuilt_clang//:strip_binaries",
        # TODO(digit): Determine appropriate `supports_param_files` value
        # after decyphering the Bazel documentation or inspecting its source
        # code. :-(
        supports_param_files = 0,
        toolchain_config = name + "_toolchain_config",
        toolchain_identifier = "prebuilt_clang",
    )

    native.toolchain(
        name = name + "_cc_toolchain",
        exec_compatible_with = [
            "@//build/bazel/platforms/os:" + host_os,
            "@//build/bazel/platforms/arch:" + host_arch,
        ],
        target_compatible_with = [
            "@//build/bazel/platforms/os:" + target_os,
            "@//build/bazel/platforms/arch:" + target_arch,
        ],
        toolchain = ":" + name,
        toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
    )

def define_host_prebuilt_clang_cc_toolchains(name, host_os, host_arch):
    """Define host C++ toolchains that target both x64 and arm64 using a prebuilt Clang installation.

    Args:
      name: A prefix for the name of the toolchains generated by this function.
      host_os: Host os string using Fuchsia conventions.
      host_arch: Host cpu architecture string, using Fuchsia conventions.
    """
    if host_os == "linux":
        _sysroot_files_x64 = ["@//:linux_sysroot_x64"]
        _sysroot_files_arm64 = ["@//:linux_sysroot_arm64"]
        _sysroot_path = "prebuilt/third_party/sysroot/linux"
    else:
        _sysroot_files_x64 = []
        _sysroot_files_arm64 = []
        _sysroot_path = ""

    prebuilt_clang_cc_toolchain(
        name = name + "_" + host_os + "_x64",
        host_os = host_os,
        host_arch = host_arch,
        target_os = host_os,
        target_arch = "x64",
        sysroot_files = _sysroot_files_x64,
        sysroot_path = _sysroot_path,
    )

    prebuilt_clang_cc_toolchain(
        name = name + "_" + host_os + "_arm64",
        host_os = host_os,
        host_arch = host_arch,
        target_os = host_os,
        target_arch = "arm64",
        sysroot_files = _sysroot_files_arm64,
        sysroot_path = _sysroot_path,
    )
