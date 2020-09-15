#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test that verifies that fconfig correctly handles properties
set -e

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

  FCONFIG_CMD="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fconfig.sh"
  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"
}

TEST_config_list() {

  expected_list="bucket=
device-ip=
device-name=
image=
emu-image=
emu-bucket="

  BT_EXPECT "${FCONFIG_CMD}" list > "${BT_TEMP_DIR}/list_stdout.txt"
  BT_EXPECT_FILE_CONTAINS "${BT_TEMP_DIR}/list_stdout.txt" "${expected_list}"
}

TEST_config_invalid_property() {
  BT_EXPECT_FAIL "${FCONFIG_CMD}" get random_property 2>"${BT_TEMP_DIR}/error_message.txt"
  BT_EXPECT_FILE_CONTAINS "${BT_TEMP_DIR}/error_message.txt" "ERROR: Invalid property name: random_property"
}

TEST_config_set() {
  BT_EXPECT "${FCONFIG_CMD}" set device-ip 8080

  value="$("${FCONFIG_CMD}" get device-ip)"

  BT_EXPECT_EQ "8080"  "${value}"
}

# Test initialization.
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fconfig.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)

BT_RUN_TESTS "$@"
