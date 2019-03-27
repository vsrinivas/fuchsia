#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
source "$(cd "${script_dir}/../../../../tools/devshell" && pwd)"/lib/vars.sh || exit $?
fx-config-read

test_out=/tmp/magma_test_out

case "$1" in
intel)
  fx-command-run cp "${script_dir}/autorun_intel" /tmp/magma_autorun
  ;;
mali)
  fx-command-run cp "${script_dir}/autorun_mali" /tmp/magma_autorun
  ;;
*)
  echo >&2 "unknown gpu: $1"
  echo >&2 "usage: $0 [intel|mali]"
  exit 1
  ;;
esac

fx-command-run shell "rm -rf ${test_out}; export GTEST_OUTPUT=xml:${test_out}/ && /boot/bin/sh /tmp/magma_autorun"

rm -rf -- "${test_out}"
mkdir "${test_out}"
fx-command-run scp "[$(get-fuchsia-device-addr)]:${test_out}/*.xml" "${test_out}"

echo "Grepping for failures:"
grep failures "${test_out}"/* | grep -v 'failures=\"0\"'
