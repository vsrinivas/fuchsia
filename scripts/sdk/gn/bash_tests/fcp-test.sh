#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test that verifies that fcp correctly discovers and invokes sfp correctly.

set -e
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/gn-bash-test-lib.sh"

# Sets up an sftp mock binary on the $PATH of any subshells.
setup_sftp() {
  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # The side effect of sftp captures the stdin and saves it to sftp.captured_stdin
  cat > "${PATH_DIR_FOR_TEST}/sftp.mock_side_effects" <<INPUT
  read line
  echo "\${line}" >> "${PATH_DIR_FOR_TEST}/sftp.captured_stdin"
INPUT
}

# Sets up a device-finder mock. The implemented mock aims to produce minimal
# output that parses correctly but is otherwise uninteresting.
setup_device_finder() {
  cat >"${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/device-finder.mock_side_effects" <<"EOF"
while (("$#")); do
  case "$1" in
  --local)
    # Emit a different address than the default so the device and the host can
    # have different IP addresses.
    echo fe80::1234%coffee
    exit
    ;;
  --full)
    echo fe80::c0ff:eec0:ffee%coffee coffee-coffee-coffee-coffee
    exit
    ;;
  esac
  shift
done

echo fe80::c0ff:eec0:ffee%coffee
EOF
}

# Helpers.

# Verifies that the correct sftp command is run by fcp.
TEST_fcp() {
  setup_sftp
  setup_device_finder

  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fcp.sh"  version.txt /tmp/version.txt

  # Verify that sftp was run correctly.
  # shellcheck disable=SC1090
  source "${PATH_DIR_FOR_TEST}/sftp.mock_state"

  gn-test-check-mock-args _ANY_ -F "${FUCHSIA_WORK_DIR}/sshconfig"  _ANY_ _ANY_ \[fe80::c0ff:eec0:ffee%coffee\]

  # copy to host
    # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fcp.sh"  --to-host /config/build-info/version version.txt

  # Verify that sftp was run correctly.
  # shellcheck disable=SC1090
  source "${PATH_DIR_FOR_TEST}/sftp.mock_state.2"

  gn-test-check-mock-args _ANY_ -F "${FUCHSIA_WORK_DIR}/sshconfig"  _ANY_ _ANY_ \[fe80::c0ff:eec0:ffee%coffee\]

  expected_cmds="put \"version.txt\" \"/tmp/version.txt\"
get \"/config/build-info/version\" \"version.txt\""

  BT_EXPECT_FILE_CONTAINS "${PATH_DIR_FOR_TEST}/sftp.captured_stdin" "${expected_cmds}"

  # Check using explicit key
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fcp.sh" --private-key other_key_file.txt  --to-host /config/build-info/version version.txt
  # shellcheck disable=SC1090
  source "${PATH_DIR_FOR_TEST}/sftp.mock_state.3"
  gn-test-check-mock-args _ANY_ -F "${FUCHSIA_WORK_DIR}/sshconfig" -i "other_key_file.txt"  _ANY_ _ANY_ \[fe80::c0ff:eec0:ffee%coffee\]
}

# Test initialization.
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fcp.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/tools/device-finder
  _isolated_path_for/sftp
)

BT_SET_UP() {
  FUCHSIA_WORK_DIR="${BT_TEMP_DIR}/scripts/sdk/gn/base/images"
}

BT_RUN_TESTS "$@"
