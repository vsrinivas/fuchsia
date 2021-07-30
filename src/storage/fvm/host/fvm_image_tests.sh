#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Smoke test which constructs a variety of FVM images and performs basic
# verification.
TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)" || exit $?
FVM_CMD="${TOOLS_DIR}/fvm"
BLOBFS_CMD="${TOOLS_DIR}/blobfs"

failures=0
function setup {
  failures=0
  export TEMP_DIR=$(mktemp -d -t fvm_image_tests.sh.XXX)
}

function teardown {
  if [ ! -z "${TEMP_DIR}" ]; then
    rm -fr "${TEMP_DIR}"
  fi
}
trap teardown EXIT

function expect_fail {
  local -i status
  eval "$@" > /dev/null
  status=$?
  if (( $status == 0 )); then
    printf "$@ returned status ${status}; expected failure\n"
    failures=$((${failures} + 1))
  fi
  return 0
}

function expect {
  local -i status
  eval "$@" > /dev/null
  status=$?
  if (( $status != 0 )); then
    printf "$@ returned status ${status}; expected success\n"
    failures=$((${failures} + 1))
  fi
  return 0
}

function run_test {
  printf "Running test $@\n"
  local -i status
  setup
  eval "$@"
  status=$?
  if (( $status != 0 || $failures > 0 )); then
    printf "Test failed: $@\n"
    status=1
  else
    printf "OK\n"
    status=0
  fi
  teardown
  return $status
}

function test_fvm_with_blobfs {
  blobfs=$(mktemp ${TEMP_DIR}/blob-XXXXX.blk)
  expect "${BLOBFS_CMD} ${blobfs} create"
  tmpfile=$(mktemp ${TEMP_DIR}/fvm-XXXXX.blk)
  expect "${FVM_CMD} ${tmpfile} create --slice 32k --blob ${blobfs}"
  rm "${tmpfile}"
  rm "${blobfs}"
  return 0
}

function test_fvm_with_blobfs_and_reserved_part {
  blobfs=$(mktemp ${TEMP_DIR}/blob-XXXXX.blk)
  expect "${BLOBFS_CMD} ${blobfs} create"
  tmpfile=$(mktemp ${TEMP_DIR}/fvm-XXXXX.blk)
  expect "${FVM_CMD} ${tmpfile} create --slice 32k --blob ${blobfs} --reserve-slices 2"
  rm "${tmpfile}"
  rm "${blobfs}"
  return 0
}

run_test test_fvm_with_blobfs
run_test test_fvm_with_blobfs_and_reserved_part
