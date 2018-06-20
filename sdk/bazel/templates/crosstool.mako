# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

package(default_visibility = ["//visibility:public"])

TARGET_CPUS = [
    % for arch in data.arches:
    "${arch.long_name}",
    % endfor
]

CC_TOOLCHAINS = [(
    cpu + "|llvm",
    ":cc-compiler-" + cpu,
) for cpu in TARGET_CPUS]

cc_toolchain_suite(
    name = "toolchain",
    toolchains = dict(CC_TOOLCHAINS),
)

filegroup(
    name = "empty",
)

filegroup(
    name = "cc-compiler-prebuilts",
    srcs = [
        "clang/bin/clang",
        "clang/bin/clang-7",
        "clang/bin/llvm-ar",
        "clang/bin/clang++",
        "clang/bin/ld.lld",
        "clang/bin/lld",
        "clang/bin/llvm-nm",
        "clang/bin/llvm-objdump",
        "clang/bin/llvm-strip",
        "clang/bin/llvm-objcopy",
    ] + glob([
        "clang/lib/clang/7.0.0/include/**",
    ]),
)

filegroup(
    name = "compile",
    srcs = [
        ":cc-compiler-prebuilts",
    ],
)

filegroup(
    name = "objcopy",
    srcs = [
        "clang/bin/llvm-objcopy",
    ],
)

[
    filegroup(
        name = "every-file-" + cpu,
        srcs = [
            ":cc-compiler-prebuilts",
            ":runtime-" + cpu,
        ],
    )
    for cpu in TARGET_CPUS
]

[
    filegroup(
        name = "link-" + cpu,
        srcs = [
            ":cc-compiler-prebuilts",
            ":runtime-" + cpu,
        ],
    )
    for cpu in TARGET_CPUS
]

[
    filegroup(
        name = "runtime-" + cpu,
        srcs = [
            "clang/lib/clang/7.0.0/lib/fuchsia/libclang_rt.builtins-" + cpu + ".a",
        ],
    )
    for cpu in TARGET_CPUS
]

[
    cc_toolchain(
        name = "cc-compiler-" + cpu,
        all_files = ":every-file-" + cpu,
        compiler_files = ":compile",
        cpu = cpu,
        dwp_files = ":empty",
        dynamic_runtime_libs = [":runtime-" + cpu],
        linker_files = ":link-" + cpu,
        objcopy_files = ":objcopy",
        static_runtime_libs = [":runtime-" + cpu],
        strip_files = ":runtime-" + cpu,
        supports_param_files = 1,
    )
    for cpu in TARGET_CPUS
]

cc_library(
    name = "sources",
    srcs = glob(["src/**"]),
    visibility = ["//visibility:public"],
)
