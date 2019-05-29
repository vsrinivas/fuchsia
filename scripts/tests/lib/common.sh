#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Test framework.
# Common functions for script tests

# Any failing assert/expect should set this to 1.
FAILED=0

function EXPECT_EQ {
  local v1="$1"
  shift
  local v2="$1"
  shift
  if [[ "${v1}" != "${v2}" ]]; then
    local msg="$@"
    if [[ -n "${msg}" ]]; then
      msg=": ${msg}"
    fi
    echo "TEST FAILURE: '${v1}' != '${v2}'${msg}"
    FAILED=1
    return 1
  fi
}

# Prints the names of all functions with a test:: prefix.
function print_test_functions {
  # "declare -F" prints all declared function names, with lines like
  # "declare -f funcname".
  declare -F \
    | grep -E '^declare -f test::' \
    | sed -e 's/^declare -f //'
}

function test_main {
  local num_tests=0
  local num_failures=0
  for t in $(print_test_functions); do
    num_tests=$(( num_tests + 1 ))
    FAILED=0
    echo "RUNNING: ${t}"
    "${t}" || FAILED=1
    if (( FAILED )); then
      num_failures=$(( num_failures + 1 ))
      echo "FAILED: ${t}"
    else
      echo "PASSED: ${t}"
    fi
  done
  if (( num_failures == 0 )); then
    echo "All ${num_tests} tests passed!"
    echo "PASS"
    return 0
  else
    echo "${num_failures}/${num_tests} tests failed"
    echo "FAIL"
    return 1
  fi
}
