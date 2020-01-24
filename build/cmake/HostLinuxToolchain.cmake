# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file contains a CMake toolchain file that can be used to compile or
# cross-compile Linux host binaries with the Fuchsia prebuilt toolchain and
# sysroot. This ensures that the generated binaries will run on a large number
# of Linux distibutions (e.g. Ubuntu 14.04 and above) without ABI issues.
#
# To use it do something like:
#
#    cd $PROJECT
#    mkdir build
#    cd build
#    cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/this/file [other-options]
#
# NOTE: The sysroot only contains link libraries for GLibc, and a few other
#       system libraries. Any other dependency should be compiled from sources.
#
# NOTE: For C++, the Fuchsia Linux sysroot only ensures that libc++.a is
#       statically linked to any shared library or executable. Due to ODR
#       violation issues, you SHOULD NOT PASS C++ std:: values between several
#       shared libraries built with this toolchain (otherwise, undefined
#       behaviour or runtime crashes may occur).

include(${CMAKE_CURRENT_LIST_DIR}/ToolchainCommon.cmake)

# Assumes this is under ${FUCHSIA_SOURCE_DIR}/build/cmake/
get_filename_component(FUCHSIA_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../ ABSOLUTE)

setup_toolchain_variables(
  FUCHSIA_SOURCE_DIR "${FUCHSIA_SOURCE_DIR}"
  CLANG_COMPILER_TARGET x86_64-linux-gnu)

if("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Linux")
  # Required to run host Linux executables during the build itself.
  # An example would be https://gitub.com/KhronosGroup/Vulkan-Loader and
  # its "asm_offset" program.
  #
  # NOTE: Alternatives have been tried unsuccesfully, i.e.:
  #
  #  With $(set CMAKE_CROSSCOMPILING_EMULATOR), the build fails because
  #  the CMake ninja/Make script tries to find the executable in the current
  #  path, as in:
  #
  #    [3/16] Generating gen_defines.asm
  #    FAILED: loader/gen_defines.asm
  #    cd /tmp/cc/build-Vulkan-Loader/loader && asm_offset GAS
  #    /bin/sh: asm_offset: command not found
  #    ninja: build stopped: subcommand failed.
  #
  # With $(set CMAKE_CROSSCOMPILING_EMULATOR ""), the build fails because
  # the shell cannot find the "" program as in:
  #
  #    [3/16] Generating gen_defines.asm
  #    FAILED: loader/gen_defines.asm
  #    cd /tmp/cc/build-Vulkan-Loader/loader && "" /tmp/cc/build-Vulkan-Loader/loader/asm_offset GAS
  #    /bin/sh: : command not found
  #    ninja: build stopped: subcommand failed.
  #
  # It seems that the root of the problem comes from how the CMake function
  # cmCustomCommandGenerator::GetArgc0Location() computes the target
  # executable's location. At this point it's unclear whether this is a CMake
  # bug or a feature.
  #
  set(CMAKE_CROSSCOMPILING_EMULATOR "/usr/bin/env")
endif()
