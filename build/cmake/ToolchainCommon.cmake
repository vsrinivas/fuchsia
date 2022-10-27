# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Common toolchain-related CMake definitions. This file should be included by
# other .cmake files to define useful functions.
#
# Need support for CMAKE_C_COMPILER_TARGET
cmake_minimum_required(VERSION 3.0)

# Set toolchain-related CMake variables in the cache or parent scope, according
# to the following parameters:
#
#  FUCHSIA_SOURCE_DIR: [required]
#      Path to the Fuchsia root source directory.
#  CLANG_COMPILER_TARGET: [optional]
#      Compiler target triple passed to Clang. If not provided, auto-detected based
#      on the current host system.
#  CLANG_BINPREFIX: [optional]
#      Prefix to all Clang toolchain binaries. If not provided, auto-detected based
#      on FUCHSIA_SOURCE_DIR, and current host system.
#  SYSROOT: [optional]
#      Sysroot path to use. If not provided, auto-detected based on CLANG_COMPILER_TARGET
#  TARGET_SYSTEM_NAME: [optional]
#      Target system name, using CMake conventions. Auto-detected from
#      CLANG_COMPILER_TARGET if not provided.
#  TARGET_SYSTEM_VERSION: [optional]
#      Target system version, using CMake conventions. Auto-detected from
#      CLANG_COMPILER_TARGET if not provided.
#  TARGET_SYSTEM_PROCESSOR: [optional]
#      Target system processor, using CMake conventions. Auto-detected from
#      CLANG_COMPILER_TARGET if not provided.
#
function(setup_toolchain_variables)
  # Ensure arguments are parsed into ARG_XXX variables.
  cmake_parse_arguments(
    ARG
    ""
    "FUCHSIA_SOURCE_DIR;CLANG_COMPILER_TARGET;CLANG_BINPREFIX;SYSROOT;TARGET_SYSTEM_NAME;TARGET_SYSTEM_VERSION;TARGET_SYSTEM_PROCESSOR"
    ""
    ${ARGN})

  if (DEFINED ARG_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "Missing values for keywords: ${ARG_KEYWORDS_MISSING_VALUES}")
  endif()

  if(NOT ARG_FUCHSIA_SOURCE_DIR)
    message(FATAL_ERROR "FUCHSIA_SOURCE_DIR is required!")
  else()
    set(FUCHSIA_SOURCE_DIR "${ARG_FUCHSIA_SOURCE_DIR}")
  endif()

  string(TOLOWER ${CMAKE_HOST_SYSTEM_PROCESSOR} HOST_PROCESSOR)
  string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} HOST_SYSTEM)

  # Find the clang binprefix if it is not provided based on the current host system.
  if(DEFINED ARG_CLANG_BINPREFIX)
    set(CLANG_BINPREFIX "${ARG_CLANG_BINPREFIX}")
  else()
    # Convert host names from Clang to Fuchsia-specific conventions.
    if(${HOST_PROCESSOR} STREQUAL "x86_64")
      set(FUCHSIA_HOST_PROCESSOR "x64")
    elseif(${HOST_PROCESSOR} STREQUAL "aarch64")
      set(FUCHSIA_HOST_PROCESSOR "arm64")
    else()
      set(FUCHSIA_HOST_PROCESSOR "${HOST_PROCESSOR}")
    endif()
    if(${HOST_SYSTEM} STREQUAL "darwin")
      set(FUCHSIA_HOST_SYSTEM "mac")
    else()
      set(FUCHSIA_HOST_SYSTEM "${HOST_SYSTEM}")
    endif()
    set(CLANG_BINPREFIX "${FUCHSIA_SOURCE_DIR}/prebuilt/third_party/clang/${FUCHSIA_HOST_SYSTEM}-${FUCHSIA_HOST_PROCESSOR}/bin/")
  endif()

  if(DEFINED ARG_CLANG_COMPILER_TARGET)
    set(CLANG_COMPILER_TARGET "${ARG_CLANG_COMPILER_TARGET}")
  else()
    if("${HOST_SYSTEM}" STREQUAL "darwin")
      set(CLANG_COMPILER_TARGET "${HOST_PROCESSOR}-apple-darwin")
    elseif("${HOST_SYSTEM}" STREQUAL "linux")
      set(CLANG_COMPILER_TARGET "${HOST_PROCESSOR}-linux-gnu")
    else()
      message(FATAL_ERROR "Please set CLANG_COMPILER_TARGET for ${HOST_SYSTEM}-${HOST_PROCESSOR}")
    endif()
  endif()

  # Set the sysroot path.
  if(DEFINED ARG_SYSROOT)
    set(SYSROOT "${ARG_SYSROOT}")
  else()
    # NOTE: Match compiler target to our prebuilt sysroot/linux/usr/lib/<target>/ directories.
    if("${CLANG_COMPILER_TARGET}" MATCHES "(x86_64|aarch64|arm|i386)-(.*-)?linux(-gnu|-gnueabihf)?")
      set(SYSROOT ${FUCHSIA_SOURCE_DIR}/prebuilt/third_party/sysroot/linux)
    else()
      message(FATAL_ERROR "No prebuilt sysroot for this target, SYSROOT needed: ${CLANG_COMPILER_TARGET}")
    endif()
    if(NOT EXISTS ${SYSROOT})
      message(FATAL_ERROR "Missing sysroot directory for ${CLANG_COMPILER_TARGET}, please use SYSROOT: ${SYSROOT}")
    endif()
  endif()

  # Set CMAKE_SYSTEM_XXX values according to CLANG_COMPILER_TARGET
  if(DEFINED ARG_TARGET_SYSTEM_NAME)
    set(TARGET_SYSTEM_NAME "${ARG_TARGET_SYSTEM_NAME}")
  else()
    if("${CLANG_COMPILER_TARGET}" MATCHES ".*-linux(-gnu)?")
      set(TARGET_SYSTEM_NAME "Linux")
    elseif("${CLANG_COMPILER_TARGET}" MATCHES ".*-darwin")
      set(TARGET_SYSTEM_NAME "Darwin")
    elseif("${CLANG_COMPILER_TARGET}" MATCHES ".*-fuchsia")
      set(TARGET_SYSTEM_NAME "Fuchsia")
    else()
      message(FATAL_ERROR "Could not determine target system name from ${CLANG_COMPILER_TARGET}, "
                          "please use TARGET_SYSTEM_NAME")
    endif()
  endif()

  if(DEFINED ARG_TARGET_SYSTEM_PROCESSOR)
    set(TARGET_SYSTEM_PROCESSOR "${ARG_TARGET_SYSTEM_PROCESSOR}")
  else()
    string(REGEX REPLACE "([A-Za-z0-9]*)-.*" "\\1" TARGET_SYSTEM_PROCESSOR "${CLANG_COMPILER_TARGET}")
  endif()

  if(DEFINED ARG_TARGET_SYSTEM_VERSION)
    set(TARGET_SYSTEM_VERSION "${ARG_TARGET_SYSTEM_VERSION}")
  else()
    # NOTE: The CMake documentations states that CMAKE_SYSTEM_VERSION MUST be
    # set if CMAKE_SYSTEM_NAME is defined. However, the current implementation
    # never checks for it, and the documentation doesn't tell what the value
    # should look like.
    #
    # Inspection of the CMake sources shows that it is set by default to
    # CMAKE_HOST_SYSTEM_VERSION which is the output of `uname -r` by default for Linux
    # and Darwin.
    #
    # Defined here for the case where a future CMake release would start checking
    # for the value explictly.
    if("${TARGET_SYSTEM_NAME}" MATCHES "Linux")
      # NOTE: This matches Debian jessie.
      set(TARGET_SYSTEM_VERSION "3.16.0-4-${TARGET_PROCESSOR}")
    elseif("${TARGET_SYSTEM_NAME}" MATCHES "Darwin")
      set(TARGET_SYSTEM_VERSION "10.10.0")
    elseif("${TARGET_SYSTEM_NAME}" MATCHES "Fuchsia")
      set(TARGET_SYSTEM_VERSION "1.0")
    else()
      message(FATAL_ERROR "Could not determine target system version from ´${TARGET_SYSTEM_NAME}' name, "
                          "please use TARGET_SYSTEM_VERSION")
    endif()
  endif()

  set(CMAKE_SYSTEM_NAME ${TARGET_SYSTEM_NAME} PARENT_SCOPE)
  set(CMAKE_SYSTEM_PROCESSOR ${TARGET_SYSTEM_PROCESSOR} PARENT_SCOPE)

  set(CMAKE_SYSTEM_VERSION ${TARGET_SYSTEM_VERSION} PARENT_SCOPE)

  # Set toolchain-specific CMAKE_XXX variables in either the cache or the parent scope.
  set(CMAKE_C_COMPILER ${CLANG_BINPREFIX}clang CACHE PATH "C compiler")
  set(CMAKE_CXX_COMPILER ${CLANG_BINPREFIX}clang++ CACHE PATH "C++ compiler")
  set(CMAKE_CXX_COMPILER ${CLANG_BINPREFIX}clang CACHE PATH "Assembler")
  set(CMAKE_LINKER ${CLANG_BINPREFIX}ld.lld CACHE PATH "Linker")
  set(CMAKE_AR ${CLANG_BINPREFIX}llvm-ar CACHE PATH "ar")
  set(CMAKE_RANLIB ${CLANG_BINPREFIX}llvm-ranlib CACHE PATH "ranlib")
  set(CMAKE_NM ${CLANG_BINPREFIX}llvm-nm CACHE PATH "nm")
  set(CMAKE_OBJCOPY ${CLANG_BINPREFIX}llvm-objcopy CACHE PATH "objcopy")
  set(CMAKE_OBJDUMP ${CLANG_BINPREFIX}llvm-objdump CACHE PATH "objdump")
  set(CMAKE_STRIP ${CLANG_BINPREFIX}strip CACHE PATH "strip")
  set(CLANG_TIDY_EXE ${CLANG_BINPREFIX}clang-tidy CACHE PATH "clang tidy")

  set(CMAKE_C_COMPILER_TARGET ${CLANG_COMPILER_TARGET} CACHE STRING "C compiler target triple")
  set(CMAKE_CXX_COMPILER_TARGET ${CLANG_COMPILER_TARGET} CACHE STRING "C++ compiler target triple")
  set(CMAKE_ASM_COMPILER_TARGET ${CLANG_COMPILER_TARGET} CACHE STRING "Assembler target triple")

  # Set the sysroot and ensure that package/library/header probing doesn't look elsewhere.
  set(CMAKE_SYSROOT ${SYSROOT} CACHE STRING "Sysroot path")

  set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT} PARENT_SCOPE)

  set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER PARENT_SCOPE)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY PARENT_SCOPE)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY PARENT_SCOPE)
endfunction()
