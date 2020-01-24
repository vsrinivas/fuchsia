# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# The following variables should be defined before including this file:
#
#  FUCHSIA_SYSTEM_PROCESSOR: Host processor name, using Clang conventions.
#
#  FUCHSIA_COMPILER_TARGET: Target compiler triple for generated
#    binaries, using GCC/Clang/llvm conventions.
#
#  FUCHSIA_SYSROOT: Location of Fuchsia sysroot to use.

# Assumes this is under ${FUCHSIA_SOURCE_DIR}/build/
include(${CMAKE_CURRENT_LIST_DIR}/cmake/ToolchainCommon.cmake)
get_filename_component(FUCHSIA_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../ ABSOLUTE)

if(NOT DEFINED FUCHSIA_COMPILER_TARGET)
  message(FATAL_ERROR "FUCHSIA_COMPILER_TARGET should be set when including this file!")
endif()

if(NOT DEFINED FUCHSIA_SYSTEM_PROCESSOR)
  message(FATAL_ERROR "FUCHSIA_SYSTEM_PROCESSOR should be set when including this file!")
endif()

if(NOT DEFINED FUCHSIA_SYSROOT)
  message(FATAL_ERROR "FUCHSIA_SYSROOT should be set when including this file!")
endif()

setup_toolchain_variables(
  FUCHSIA_SOURCE_DIR "${FUCHSIA_SOURCE_DIR}"
  CLANG_COMPILER_TARGET "${FUCHSIA_COMPILER_TARGET}"
  SYSROOT "${FUCHSIA_SYSROOT}"
  TARGET_SYSTEM_NAME "Fuchsia"
  TARGET_SYSTEM_PROCESSOR "${FUCHSIA_SYSTEM_PROCESSOR}"
  TARGET_SYSTEM_VERSION "1.0")
