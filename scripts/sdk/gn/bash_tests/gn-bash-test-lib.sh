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

