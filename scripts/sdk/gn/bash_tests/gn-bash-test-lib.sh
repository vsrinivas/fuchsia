#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Collection of utility functions to help test the bash scripts. The contents
# of this library are prefixed with gn-test.
#
# This file expects the Bash Test Framework to be sourced. Due to the use of
# BT_EXPECT_EQ and related functions.

set -e

function is-mac {
  [[ "$(uname -s)" == "Darwin" ]] && return 0
  return 1
}

# Runs md5sum or equivalent on mac.
function run-md5 {
  if is-mac; then
    MD5_CMD=("/sbin/md5"  "-r")
  else
    MD5_CMD=("md5sum")
  fi

  MD5_CMD+=("$@")

  "${MD5_CMD[@]}"
}

if is-mac; then
  realpath() {
      [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
  }
fi


# Runs a bash script. The function provides these conveniences over calling the
# script directly:
#
# * Rather than calling the bash script directly, this command explicitly
#   invokes Bash and propagates some option flags.
# * Rather than showing the bash output, this command only outputs output if a
#   test fails.
#
# Args: the script to run and all args to pass.
gn-test-run-bash-script() {
  local shell_flags
  # propagate certain bash flags if present
  shell_flags=()
  if [[ $- == *x* ]]; then
    shell_flags+=( "-x" )
  fi
  local output

  if ! output=$(bash "${shell_flags[@]}" "$@" 2>&1); then
    echo "${output}"
    return 1
  fi
  return 0
}

# Verifies that the arguments in BT_MOCK_ARGS match the arguments to this function.
# The number and order of arguments must match, or non-zero is returned.
# If a value of an argument is un-important it can be marked with the string
# _ANY_. This allows for matching arguments that may have unique values, such as temp
# filenames.
# Args: The expected arguments.
# Returns: 0 if found; 1 if not found.
gn-test-check-mock-args() {
  BT_EXPECT_EQ "$#" "${#BT_MOCK_ARGS[@]}"
  local expected=("$@")
  local actual=("${BT_MOCK_ARGS[@]}")
  for (( i=0; i<"${#expected[@]}"; i++ )); do
    if [[ "${expected[$i]}" != "_ANY_" ]];  then
      BT_EXPECT_EQ "${actual[$i]}" "${expected[$i]}"
    fi
  done
  return 0
}

# Verifies that the given arguments appear in the command line invocation of the
# most previously sourced mock state. Any arguments passed to this function will
# be searched for in the actual arguments. This succeeds if the arguments are
# found in adjacent positions in the correct order.
#
# This function only checks for presence. As a result, it will NOT verify any of
# the following:
#
# * The arguments only appear once.
# * The arguments don't appear with conflicting arguments.
# * Any given argument --foo isn't overridden, say with a --no-foo flag later.
#
# Args: any number of arguments to check.
# Returns: 0 if found; 1 if not found.
gn-test-check-mock-partial() {
  local expected=("$@")
  for j in "${!BT_MOCK_ARGS[@]}"; do
    local window=("${BT_MOCK_ARGS[@]:$j:${#expected}}")
    local found=true
    for k in "${!expected[@]}"; do
      if [[ "${expected[$k]}" != "${window[$k]}" ]]; then
        found=false
        break
      fi
    done
    if [[ "${found}" == "true" ]]; then
      return 0
    fi
  done
  BT_EXPECT false "Could not find expected:\n${expected[*]}\nin arguments:\n${BT_MOCK_ARGS[*]}"
  return 1
}


# Returns the machine architecture specific subdirectory for tools in the sdk.
function gn-test-tools-subdir {
  local machine
  machine="$(uname -m)"
  local dir
  case "${machine}" in
  x86_64)
    dir="tools/x64"
    ;;
  aarch64*)
    dir="tools/arm64"
    ;;
  armv8*)
    dir="tools/arm64"
    ;;
  *)
    dir="tools/${machine}"
    ;;
  esac
  echo "${dir}"
}