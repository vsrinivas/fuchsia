#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests for fuchsia-common.sh library script

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

BT_FILE_DEPS=(
  "scripts/sdk/gn/base/bin/fuchsia-common.sh"
  "scripts/sdk/gn/base/bin/sshconfig"
  "scripts/sdk/gn/testdata/meta/manifest.json"
)
SOURCE_FILE="${SCRIPT_SRC_DIR}/../base/bin/fuchsia-common.sh"
REPO_ROOT="$(realpath "$(dirname "${SOURCE_FILE}")")"

MOCKED_SSH_BIN="mocked/ssh"
MOCKED_DEVICE_FINDER="tools/device-finder"
MOCKED_GSUTIL="${REPO_ROOT}/gsutil"
BT_MOCKED_TOOLS=(
   "${MOCKED_SSH_BIN}"
   "${MOCKED_DEVICE_FINDER}"
   "${MOCKED_GSUTIL}"
)

BT_SET_UP() {
    btf::make_mock "${MOCKED_SSH_BIN}"
    cat > "${MOCKED_SSH_BIN}.mock_side_effects" <<"EOF"
      echo "$@"
EOF
}

source "${SOURCE_FILE}"


TEST_fx-warn() {
    BT_ASSERT_FUNCTION_EXISTS fx-warn
    fx-warn "This is a test" 2>fx-warn-stderr.txt > fx-warn-stdout.txt
    BT_EXPECT_FILE_CONTAINS "fx-warn-stderr.txt" "WARNING: This is a test"

    [ ! -s fx-warn-stdout.txt ] || BT_EXPECT_GOOD_STATUS $?
}

TEST_fx-error() {
    BT_ASSERT_FUNCTION_EXISTS fx-error
    fx-error "This is a test" 2>fx-error-stderr.txt > fx-error-stdout.txt
    BT_EXPECT_FILE_CONTAINS "fx-error-stderr.txt" "ERROR: This is a test"

    [ ! -s fx-error-stdout.txt ] || BT_EXPECT_GOOD_STATUS $?
}

TEST_ssh-cmd() {
     BT_ASSERT_FUNCTION_EXISTS ssh-cmd
     BT_ASSERT_FUNCTION_EXISTS set-ssh-path

    set-ssh-path "${MOCKED_SSH_BIN}"
    ssh-cmd remote-host ls -l
    source "${MOCKED_SSH_BIN}.mock_state"
    EXPECTED_SSH_CMD_LINE=("${MOCKED_SSH_BIN}" "-F" "${REPO_ROOT}/sshconfig" "remote-host" "ls" "-l")
    BT_EXPECT_EQ "${EXPECTED_SSH_CMD_LINE[*]}" "${BT_MOCK_ARGS[*]}"
}

TEST_get-device-name() {
     BT_ASSERT_FUNCTION_EXISTS get-device-name
         btf::make_mock "${MOCKED_DEVICE_FINDER}"
    cat > "${MOCKED_DEVICE_FINDER}.mock_side_effects" <<"EOF"
      echo fe80::4607:bff:fe69:b53e%enx44070b69b53f atom-device-name-mocked
EOF
    DEVICE_NAME="$(get-device-name ".")"
    BT_EXPECT_EQ "${DEVICE_NAME}" "atom-device-name-mocked"
}

TEST_get-host-ip() {
     BT_ASSERT_FUNCTION_EXISTS get-host-ip
    btf::make_mock "${MOCKED_DEVICE_FINDER}"
    cat > "${MOCKED_DEVICE_FINDER}.mock_side_effects" <<"EOF"
      echo "fe80::4600:fff:fefe:b555%enx010101010101"
EOF
    HOST_IP="$(get-host-ip ".")"
    BT_EXPECT_EQ "${HOST_IP}" "fe80::4600:fff:fefe:b555"
}

TEST_get-sdk-version() {
  BT_ASSERT_FUNCTION_EXISTS get-sdk-version
  SDK_VERSION="$(get-sdk-version "${SCRIPT_SRC_DIR}/../../testdata")"
  BT_EXPECT_EQ "${SDK_VERSION}" "8890101742249547440"
}

TEST_run-gsutil() {
  BT_ASSERT_FUNCTION_EXISTS run-gsutil
  btf::make_mock "${MOCKED_GSUTIL}"
  cat > "${MOCKED_GSUTIL}.mock_side_effects" <<EOF
    echo "gs://fuchsia/development/LATEST"
EOF
  RESULT="$(run-gsutil ls gsutil ls gs://fuchsia/development/LATEST)"
  BT_EXPECT_EQ "${RESULT}" "gs://fuchsia/development/LATEST"
}

TEST_get-available-images() {
  BT_ASSERT_FUNCTION_EXISTS get-available-images
  btf::make_mock "${MOCKED_GSUTIL}"
  cat > "${MOCKED_GSUTIL}.mock_side_effects" <<"EOF"
    if [[ "${2}" == gs://fuchsia* ]]; then
      echo "gs://fuchsia/development/sdk_id/images/image1.tgz"
      echo "gs://fuchsia/development/sdk_id/images/image2.tgz"
      echo "gs://fuchsia/development/sdk_id/images/image3.tgz"
    elif [[ "${2}" == gs://other* ]]; then
      echo "gs://other/development/sdk_id/images/image4.tgz"
      echo "gs://other/development/sdk_id/images/image5.tgz"
      echo "gs://other/development/sdk_id/images/image6.tgz"
    fi
EOF
  RESULT_LIST=()
  IFS=' ' read -r -a RESULT_LIST <<< "$(get-available-images "sdk_id")"
  BT_EXPECT_EQ "${RESULT_LIST[*]}" "image1 image2 image3"

  IFS=' ' read -r -a RESULT_LIST <<< "$(get-available-images "sdk_id" "other")"
  BT_EXPECT_EQ "${RESULT_LIST[*]}" "image4 image5 image6 image1 image2 image3"
}

BT_RUN_TESTS "$@"
