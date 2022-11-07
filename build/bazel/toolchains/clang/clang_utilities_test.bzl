# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for clang_utilities.bzl"""

load("@bazel_skylib//lib:unittest.bzl", "asserts", "unittest")
load(
    "//:build/bazel/toolchains/clang/clang_utilities.bzl",
    "process_clang_builtins_output",
)

def _process_clang_builtins_output_test_impl(ctx):
    response = """Fuchsia clang version 16.0.0 (https://llvm.googlesource.com/llvm-project 039b969b32b64b64123dce30dd28ec4e343d893f)
Target: x86_64-unknown-linux-gnu
Thread model: posix
InstalledDir: /usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/bin
Found candidate GCC installation: /usr/lib/gcc/x86_64-linux-gnu/11
Found candidate GCC installation: /usr/lib/gcc/x86_64-linux-gnu/12
Selected GCC installation: /usr/lib/gcc/x86_64-linux-gnu/12
Candidate multilib: .;@m64
Candidate multilib: 32;@m32
Candidate multilib: x32;@mx32
Selected multilib: .;@m64
 (in-process)
 "/usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/bin/clang-16" -cc1 -triple x86_64-unknown-linux-gnu -E -disable-free -clear-ast-before-backend -disable-llvm-verifier -discard-value-names -main-file-name empty -mrelocation-model pic -pic-level 2 -pic-is-pie -mframe-pointer=all -fmath-errno -ffp-contract=on -fno-rounding-math -mconstructor-aliases -funwind-tables=2 -target-cpu x86-64 -tune-cpu generic -debugger-tuning=gdb -v -fcoverage-compilation-dir=/usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang -resource-dir /usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/lib/clang/16.0.0 -internal-isystem /usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/bin/../include/x86_64-unknown-linux-gnu/c++/v1 -internal-isystem /usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/bin/../include/c++/v1 -internal-isystem /usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/lib/clang/16.0.0/include -internal-isystem /usr/local/include -internal-isystem /usr/lib/gcc/x86_64-linux-gnu/12/../../../../x86_64-linux-gnu/include -internal-externc-isystem /usr/include/x86_64-linux-gnu -internal-externc-isystem /include -internal-externc-isystem /usr/include -fdeprecated-macro -fdebug-compilation-dir=/usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang -ferror-limit 19 -fgnuc-version=4.2.1 -fcxx-exceptions -fexceptions -faddrsig -D__GCC_HAVE_DWARF2_CFI_ASM=1 -o - -x c++ ./empty
clang -cc1 version 16.0.0 based upon LLVM 16.0.0git default target x86_64-unknown-linux-gnu
ignoring nonexistent directory "/usr/lib/gcc/x86_64-linux-gnu/12/../../../../x86_64-linux-gnu/include"
ignoring nonexistent directory "/include"
#include "..." search starts here:
#include <...> search starts here:
 /usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/bin/../include/x86_64-unknown-linux-gnu/c++/v1
 /usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/bin/../include/c++/v1
 /usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/lib/clang/16.0.0/include
 /usr/local/include
 /usr/include/x86_64-linux-gnu
 /usr/include
End of search list.
"""
    expect = (
        "16",
        "16.0.0",
        [
            "/usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/bin/../include/x86_64-unknown-linux-gnu/c++/v1",
            "/usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/bin/../include/c++/v1",
            "/usr/local/home/user/fuchsia/out/default/gen/build/bazel/output_base/external/prebuilt_clang/lib/clang/16.0.0/include",
        ],
    )
    env = unittest.begin(ctx)
    asserts.equals(env, expect, process_clang_builtins_output(response))
    return unittest.end(env)

process_clang_builtins_output_test = unittest.make(_process_clang_builtins_output_test_impl)

def include_clang_utilities_test_suite(name):
    unittest.suite(
        name,
        process_clang_builtins_output_test,
    )
