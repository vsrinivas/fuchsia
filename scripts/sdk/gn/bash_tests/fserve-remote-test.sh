#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test that verifies that fserve-remote builds the ssh commands correctly.

set -e
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/gn-bash-test-lib.sh"

FSERVE_REMOTE_CMD="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fserve-remote.sh"
FCONFIG_CMD="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fconfig.sh"

# Sets up an ssh mock binary on the $PATH of any subshell.
set_up_ssh() {
  export PATH="${BT_TEMP_DIR}/isolated_path_for:${PATH}"

  SSH_MOCK_PATH="${BT_TEMP_DIR}/isolated_path_for"

  cat > "${SSH_MOCK_PATH}/ssh.mock_side_effects" <<"EOF"
    if [[ "$*" =~ SSH_CONNECTION ]]; then
      echo "172.20.100.10 38618 100.90.250.100 22"
    elif [[ "$*" =~ "amber_ctl add_src" ]]; then
      return 0
    else
      echo "$@"
    fi
EOF
}

# Sets up a device-finder mock. The implemented mock aims to produce minimal
# output that parses correctly but is otherwise uninteresting.
set_up_device_finder() {
  cat >"${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/device-finder.mock_side_effects" <<"EOF"
 if [[ "$*" =~ -local ]]; then
    # Emit a different address than the default so the device and the host can
    # have different IP addresses.
    echo fe80::1234%coffee
  elif [[ "$*" =~ -full ]]; then
    echo fe80::c0ff:eec0:ffee%coffee coffee-coffee-coffee-coffee
  else
    echo fe80::c0ff:eec0:ffee%coffee
fi
EOF
}

# Verify that pm serve was run correctly.
TEST_fserve_remote() {
  set_up_ssh
  set_up_device_finder

  REMOTE_PATH="/home/path_to_samples/third_party/fuchsia-sdk"

  # Run command.
  BT_EXPECT "${FSERVE_REMOTE_CMD}" glinux.google.com "${REMOTE_PATH}" > "${BT_TEMP_DIR}/fserve_remote_log.txt" 2>&1

  # First SSH is to check if the mux session to the host exists
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected=(_ANY_ "-O" "check" "glinux.google.com")

  expected_ssh_args=("${SSH_MOCK_PATH}/ssh" -6 -L "\*:8083:localhost:8083" -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22" -o "ExitOnForwardFailure=yes" "glinux.google.com")

  # Next SSH is checking socket status
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected=("${expected_ssh_args[@]}")
  expected+=("ss -ln | grep :8083")
  gn-test-check-mock-args "${expected[@]}"

  # Next SSH is to kill pm
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.3"
  expected=("${expected_ssh_args[@]}")
  expected+=("pkill -u \$USER pm")
  gn-test-check-mock-args "${expected[@]}"

  # Last  SSH is to start up pm
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.4"
  expected=("${expected_ssh_args[@]}")
  expected+=(cd  "${REMOTE_PATH}" "&&" "./bin/fconfig.sh" "set" "device-ip" 127.0.0.1 "&&" "./bin/fserve.sh")
  gn-test-check-mock-args "${expected[@]}"
}

TEST_fserve_remote_with_config() {
  set_up_ssh
  set_up_device_finder

  REMOTE_PATH="/home/path_to_samples/third_party/fuchsia-sdk"

  BT_EXPECT "${FCONFIG_CMD}" set image "test-image"
  BT_EXPECT "${FCONFIG_CMD}" set bucket "custom-bucket"
  
  # Run command.
  BT_EXPECT "${FSERVE_REMOTE_CMD}" glinux.google.com "${REMOTE_PATH}" > "${BT_TEMP_DIR}/fserve_remote_with_config_log.txt" 2>&1

  # First SSH is to check if the mux session to the host exists
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected=(_ANY_ "-O" "check" "glinux.google.com")
  gn-test-check-mock-args "${expected[@]}"

  expected_ssh_args=("${SSH_MOCK_PATH}/ssh" -6 -L "\*:8083:localhost:8083" -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22" -o "ExitOnForwardFailure=yes" "glinux.google.com")

  # Next SSH is checking socket status
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected=("${expected_ssh_args[@]}")
  expected+=("ss -ln | grep :8083")
  gn-test-check-mock-args "${expected[@]}"

  # Next SSH is to kill pm
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.3"
  expected=("${expected_ssh_args[@]}")
  expected+=("pkill -u \$USER pm")
  gn-test-check-mock-args "${expected[@]}"

  # Last  SSH is to start up pm
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.4"
  expected=("${expected_ssh_args[@]}")
  expected+=(cd  "${REMOTE_PATH}" "&&" "./bin/fconfig.sh" "set" "device-ip" 127.0.0.1 "&&" "./bin/fserve.sh" "--bucket" "custom-bucket" "--image" "test-image")
  gn-test-check-mock-args "${expected[@]}"
}

# Test initialization.
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fserve-remote.sh
  scripts/sdk/gn/base/bin/fconfig.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/bin/gsutil
  scripts/sdk/gn/base/tools/device-finder
  scripts/sdk/gn/base/tools/pm
  isolated_path_for/ssh
)

BT_INIT_TEMP_DIR() {
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta"

  # Create a stub SDK manifest.
  cp "${BT_DEPS_ROOT}/scripts/sdk/gn/testdata/meta/manifest.json" \
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta/manifest.json"
}

BT_RUN_TESTS "$@"
