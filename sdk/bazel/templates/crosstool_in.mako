# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

major_version: "1.x.x"
minor_version: "llvm:7.x.x"
default_target_cpu: "${data.arches[0].short_name}"

% for arch in data.arches:
default_toolchain {
  cpu: "${arch.short_name}"
  toolchain_identifier: "crosstool-1.x.x-llvm-fuchsia-${arch.short_name}"
}

% endfor

% for arch in data.arches:
toolchain {
  abi_version: "local"
  abi_libc_version: "local"

  builtin_sysroot: "%{SYSROOT_${arch.short_name.upper()}}"
  compiler: "llvm"
  default_python_top: "/dev/null"
  default_python_version: "python2.7"
  host_system_name: "x86_64-unknown-linux-gnu"
  needsPic: true
  supports_gold_linker: false
  supports_incremental_linker: false
  supports_fission: false
  supports_interface_shared_objects: false
  supports_normalizing_ar: true
  supports_start_end_lib: false
  target_libc: "fuchsia"
  target_cpu: "${arch.long_name}"
  target_system_name: "${arch.long_name}-fuchsia"
  toolchain_identifier: "crosstool-1.x.x-llvm-fuchsia-${arch.short_name}"
  cc_target_os: "fuchsia"

  tool_path { name: "ar" path: "clang/bin/llvm-ar" }
  tool_path { name: "cpp" path: "clang/bin/clang++" }
  tool_path { name: "gcc" path: "clang/bin/clang" }
  tool_path { name: "lld" path: "clang/bin/lld" }
  tool_path { name: "objdump" path: "clang/bin/llvm-objdump" }
  tool_path { name: "strip" path: "clang/bin/llvm-strip" }
  tool_path { name: "nm" path: "clang/bin/llvm-nm" }
  tool_path { name: "objcopy" path: "clang/bin/llvm-objcopy" }
  tool_path { name: "dwp" path: "/not_available/dwp" }        # Not used but required
  tool_path { name: "compat-ld" path: "/not_available/compat-ld" }  # Not used but required
  tool_path { name: "gcov" path: "/not_available/gcov" }       # Not used but required
  tool_path { name: "gcov-tool" path: "/not_available/gcov-tool" }  # Not used but required
  tool_path { name: "ld" path: "clang/bin/ld.lld" }

  compiler_flag: "--target=${arch.long_name}-fuchsia"
  linker_flag: "--target=${arch.long_name}-fuchsia"

  cxx_flag: "-std=c++14"
  cxx_flag: "-xc++"

  linker_flag: "--driver-mode=g++"
  linker_flag: "-lzircon"

  ### start 'sdk' portion
  # The following are to make the various files in runtimes/sdk available

  # Implicit dependencies for Fuchsia system functionality
  cxx_builtin_include_directory: "%{SYSROOT_${arch.short_name.upper()}}/include" # Platform parts of libc.
  cxx_builtin_include_directory: "%{CROSSTOOL_ROOT}/clang/lib/${arch.long_name}-fuchsia/include/c++/v1" # Platform libc++.
  cxx_builtin_include_directory: "%{CROSSTOOL_ROOT}/clang/lib/clang/7.0.0/include" # Platform libc++.
  ### end

  #### Common compiler options. ####

  compiler_flag: "-Wall"
  compiler_flag: "-Werror"
}

% endfor
