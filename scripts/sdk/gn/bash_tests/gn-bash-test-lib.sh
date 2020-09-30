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


# Custom mock logger. This is needed when there are calls to the
# same mock in the backgroun and foreground close to each other.
#
# Args:
#   FILENAME: the file to write the mock state to. If the same mock is
#     called multiple times, the invocation ordinal is appended.
#
#   RC: the return code the mock is returning.
#
#   Remainder of args are recorded as arguments passed to the mock.
#
function  gn-test-log-mock  {
  FILENAME="$1"
  shift
  RC="$1"
  shift

  declare state_file="${FILENAME}"
  if [[ -e "${state_file}" ]]; then
    # Command was executed more than once. Use numeric suffixes.
    mv "${state_file}" "${state_file}.1"
    state_file="${state_file}.2"
  elif [[ -e "${state_file}.1" ]]; then
    declare -i index
    declare -i max_index=1
    for file in "${state_file}".*; do
      [[ -e $file ]] || break # handle no files found.
      index=${file##*.}
      max_index=$(( index > max_index ? index : max_index ))
    done
    state_file="${state_file}.$((max_index+1))"
  fi

  # Write the args into the status file.
  #
  # This is split into three steps, the middle of which writes the Bash array
  # literal. The array is written using printf and %q to quote or escape the
  # elements of the $@ array. This is important for a number of reasons:
  #
  # * Using escaped double quotes around $@ causes all of the arguments to be
  #   concatenated into a single space-separated string.
  # * Using escaped double quotes isn't safe if any item in the array contains a
  #   double quotation mark.
  # * Using printf allows all strings to be safely included in the array.
  # * Using printf prevents variable expansion when the status file is sourced as
  #   a script.
  {
    echo "#!/bin/bash"
    printf "BT_MOCK_ARGS=( "
    printf "%q " "${0}" "$@"
    printf ")\n"
    echo return "$RC"
  } >> "${state_file}"
}

# Returns the latest name for the mock file given.
# The Bash test framework creates a file named <mock_state> (by default
# <tool>.mock_state) for the first execution of the mock. For a second
# execution, it renames the first file to <mock_state>.1 and creates a
# <mock_state>.2. After that, each subsequent execution of the mock creates
# a file named <mock_state>.<n>. For most tests, it is relevant to check
# the n-th specific execution, but in some rare cases, for example when the
# mock is executed in a loop, the test can care only about the latest execution
# and finding it is a tedious process. This method helps with that, returning
# <mock_state> if there was none or only a single invocation of the mock, or
# <mock_state>.<n> if there were <n> executions of the mock.
#
# Args:
#   name: the mock state filename without the ".<n>" suffix.
#
function gn-test-latest-mock {
  local path="$1"
  if [[ -f "$path" ]]; then
    echo "$path"
  else
    # find the number of dots in the mock state path:
    local dots="${path//[^.]}"
    # the key will be the last dot-separated token of numbered mock_state's, so:
    #   my.path.with.dots/my.tool.mock_state
    # will be keyed by
    #   (# dots before numbering + 1 dot to separate the number + 1) = 7th field
    # my.path.with.dots/my.tool.mock_state.10 and
    # my.path.with.dots/my.tool.mock_state.9 will be correctly sorted by
    # numeric order.
    local key=$(( ${#dots} + 1 + 1 ))

    find "$(dirname "${path}")" -mindepth 1 -maxdepth 1 \
        -name "$(basename "${path}")\.[0-9]*" 2>/dev/null \
      | sort -t. -k${key} -n -r \
      | head -1
  fi
}
