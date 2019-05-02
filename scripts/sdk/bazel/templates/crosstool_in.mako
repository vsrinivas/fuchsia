# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

major_version: "1.x.x"
minor_version: "llvm:7.x.x"
default_target_cpu: "${data.arches[0].short_name}"

% for arch in data.arches:
toolchain {
  toolchain_identifier: "crosstool-1.x.x-llvm-fuchsia-${arch.long_name}"
  host_system_name: "x86_64-unknown-linux-gnu"
  target_system_name: "${arch.long_name}-fuchsia"
  target_cpu: "${arch.long_name}"
  target_libc: "fuchsia"
  compiler: "llvm"
  abi_version: "local"
  abi_libc_version: "local"
  tool_path {
    name: "ar"
    path: "clang/bin/llvm-ar"
  }
  tool_path {
    name: "cpp"
    path: "clang/bin/clang++"
  }
  tool_path {
    name: "gcc"
    path: "clang/bin/clang"
  }
  tool_path {
    name: "lld"
    path: "clang/bin/lld"
  }
  tool_path {
    name: "objdump"
    path: "clang/bin/llvm-objdump"
  }
  tool_path {
    name: "strip"
    path: "clang/bin/llvm-strip"
  }
  tool_path {
    name: "nm"
    path: "clang/bin/llvm-nm"
  }
  tool_path {
    name: "objcopy"
    path: "clang/bin/llvm-objcopy"
  }
  tool_path {
    name: "dwp"
    path: "/not_available/dwp"
  }
  tool_path {
    name: "compat-ld"
    path: "/not_available/compat-ld"
  }
  tool_path {
    name: "gcov"
    path: "/not_available/gcov"
  }
  tool_path {
    name: "gcov-tool"
    path: "/not_available/gcov-tool"
  }
  tool_path {
    name: "ld"
    path: "clang/bin/ld.lld"
  }
  # Implicit dependencies for Fuchsia system functionality
  cxx_builtin_include_directory: "%{SYSROOT_${arch.short_name.upper()}}/include" # Platform parts of libc.
  cxx_builtin_include_directory: "%{CROSSTOOL_ROOT}/clang/lib/${arch.long_name}-fuchsia/include/c++/v1" # Platform libc++.
  cxx_builtin_include_directory: "%{CROSSTOOL_ROOT}/clang/lib/clang/8.0.0/include" # Platform libc++.
  ### end
  builtin_sysroot: "%{SYSROOT_${arch.short_name.upper()}}"
  feature {
    name: "default_compile_flags"
    flag_set {
      action: "assemble"
      action: "preprocess-assemble"
      action: "linkstamp-compile"
      action: "c-compile"
      action: "c++-compile"
      action: "c++-header-parsing"
      action: "c++-module-compile"
      action: "c++-module-codegen"
      action: "lto-backend"
      action: "clif-match"
      flag_group {
        flag: "--target=${arch.long_name}-fuchsia"
        flag: "-Wall"
        flag: "-Werror"
        flag: "-Wextra-semi"
      }
    }
    flag_set {
      action: "linkstamp-compile"
      action: "c++-compile"
      action: "c++-header-parsing"
      action: "c++-module-compile"
      action: "c++-module-codegen"
      action: "lto-backend"
      action: "clif-match"
      flag_group {
        flag: "-std=c++14"
        flag: "-xc++"
      }
    }
    enabled: true
  }
  feature {
    name: "default_link_flags"
    flag_set {
      action: "c++-link-executable"
      action: "c++-link-dynamic-library"
      action: "c++-link-nodeps-dynamic-library"
      flag_group {
        flag: "--target=${arch.long_name}-fuchsia"
        flag: "--driver-mode=g++"
        flag: "-lzircon"
      }
    }
    enabled: true
  }
  feature {
    name: "supports_pic"
    enabled: true
  }
  cc_target_os: "fuchsia"
}
% endfor
