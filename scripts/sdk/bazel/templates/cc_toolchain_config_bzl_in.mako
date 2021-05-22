load("@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl", "tool_path", "feature", "flag_set", "flag_group")

def _cc_toolchain_config_impl(ctx):
    target_system_name = ctx.attr.cpu + "-fuchsia"
    tool_paths = [
        tool_path(
            name = "ar",
            path = "clang/bin/llvm-ar",
        ),
        tool_path(
            name = "cpp",
            path = "clang/bin/clang++",
        ),
        tool_path(
            name = "gcc",
            path = "clang/bin/clang",
        ),
        tool_path(
            name = "lld",
            path = "clang/bin/lld",
        ),
        tool_path(
            name = "objdump",
            path = "clang/bin/llvm-objdump",
        ),
        tool_path(
            name = "strip",
            path = "clang/bin/llvm-strip",
        ),
        tool_path(
            name = "nm",
            path = "clang/bin/llvm-nm",
        ),
        tool_path(
            name = "objcopy",
            path = "clang/bin/llvm-objcopy",
        ),
        tool_path(
            name = "dwp",
            path = "/not_available/dwp",
        ),
        tool_path(
            name = "compat-ld",
            path = "/not_available/compat-ld",
        ),
        tool_path(
            name = "gcov",
            path = "/not_available/gcov",
        ),
        tool_path(
            name = "gcov-tool",
            path = "/not_available/gcov-tool",
        ),
        tool_path(
            name = "ld",
            path = "clang/bin/ld.lld",
        ),
    ]
    features = [
        feature(
            name = "default_compile_flags",
            flag_sets = [
                flag_set(
                    actions = [
                        "assemble",
                        "preprocess-assemble",
                        "linkstamp-compile",
                        "c-compile",
                        "c++-compile",
                        "c++-header-parsing",
                        "c++-module-compile",
                        "c++-module-codegen",
                        "lto-backend",
                        "clif-match",
                    ],
                    flag_groups = [
                        flag_group(
                            flags = [
                                "--target=" + target_system_name,
                                "-Wall",
                                "-Werror",
                                "-Wextra-semi",
                                "-Wnewline-eof",
                                "-Wshadow",
                            ],
                        ),
                    ],
                ),
                flag_set(
                    actions = [
                        "linkstamp-compile",
                        "c++-compile",
                        "c++-header-parsing",
                        "c++-module-compile",
                        "c++-module-codegen",
                        "lto-backend",
                        "clif-match",
                    ],
                    flag_groups = [
                        flag_group(
                            flags = [
                                "-std=c++14",
                                "-xc++",
                                # Needed to compile shared libraries.
                                "-fPIC",
                            ],
                        ),
                    ],
                ),
            ],
            enabled = True,
        ),
        feature(
            name = "default_link_flags",
            flag_sets = [
                flag_set(
                    actions = [
                        "c++-link-executable",
                        "c++-link-dynamic-library",
                        "c++-link-nodeps-dynamic-library",
                    ],
                    flag_groups = [
                        flag_group(
                            flags = [
                                "--target=" + target_system_name,
                                "--driver-mode=g++",
                                "-lzircon",
                            ],
                        ),
                    ],
                ),
            ],
            enabled = True,
        ),
        feature(
            name = "supports_pic",
            enabled = True,
        ),
    ]
    sysroots = {
      "aarch64": "%{SYSROOT_aarch64}",
      "x86_64": "%{SYSROOT_x86_64}",
    }
    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        toolchain_identifier = "crosstool-1.x.x-llvm-fuchsia-" + ctx.attr.cpu,
        host_system_name = "x86_64-unknown-linux-gnu",
        target_system_name = target_system_name,
        target_cpu = ctx.attr.cpu,
        target_libc = "fuchsia",
        compiler = "llvm",
        abi_version = "local",
        abi_libc_version = "local",
        tool_paths = tool_paths,
        # Implicit dependencies for Fuchsia system functionality
        cxx_builtin_include_directories = [
            sysroots[ctx.attr.cpu] + "/include", # Platform parts of libc.
            "%{CROSSTOOL_ROOT}/clang/lib/" + target_system_name + "/include/c++/v1", # Platform libc++.
            "%{CROSSTOOL_ROOT}/clang/lib/clang/8.0.0/include", # Platform libc++.
        ],
        builtin_sysroot = sysroots[ctx.attr.cpu],
        features = features,
        cc_target_os = "fuchsia",
    )

cc_toolchain_config = rule(
    implementation = _cc_toolchain_config_impl,
    attrs = {
      "cpu": attr.string(mandatory=True, values=["aarch64", "x86_64"])
    },
    provides = [CcToolchainConfigInfo],
)
