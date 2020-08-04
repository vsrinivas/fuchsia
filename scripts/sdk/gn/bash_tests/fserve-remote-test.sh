#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test that verifies that fserve-remote builds the ssh commands correctly.

set -e

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
  cat >"${MOCKED_DEVICE_FINDER}.mock_side_effects" <<"EOF"
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

# Create a mock that simulates a clean environment, nothing is running.
  cat > "${SSH_MOCK_PATH}/ssh.mock_side_effects" <<"EOF"
    if [[ "$*" =~ SSH_CONNECTION ]]; then
      echo "172.20.100.10 38618 100.90.250.100 22"
      return 0
    elif [[ "$*" =~ "amber_ctl add_src" ]]; then
      return 0
    fi

    # No existing session.
    if [[ "$*" =~ "-O check" ]]; then
      return 255
    fi

    # No existing forwarding for 8022
    if [[ "$*" =~ "grep :8022" ]]; then
      return 2
    fi

    # No existing forwarding for 8083
    if [[ "$*" =~ "grep :8083" ]]; then
      return 2
    fi
    echo $@
EOF

  # Run command.
  BT_EXPECT "${FSERVE_REMOTE_CMD}" desktop.example.com "${REMOTE_PATH}" --ttl 0 > "${BT_TEMP_DIR}/fserve_remote_log.txt" 2>&1

  # Common command line args, used for each call to ssh
  ssh_common_expected=(_ANY_  "desktop.example.com" )
  ssh_common_expected+=(-S "${HOME}/.ssh/control-fuchsia-fx-remote")
  ssh_common_expected+=(-o "ControlMaster=auto")
  ssh_common_expected+=(-t)

  # First SSH is to check if the mux session to the host exists
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected=("${ssh_common_expected[@]}" -O check)
  gn-test-check-mock-args "${expected[@]}"

  # Check for an existing forwarding for port 8022.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected=("${ssh_common_expected[@]}" "ss -ln | grep :8022")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.3"
  expected=("${ssh_common_expected[@]}" "cd \$HOME" "&&"
    "cd /home/path_to_samples/third_party/fuchsia-sdk" "&&"
    "./bin/fconfig.sh set device-ip 127.0.0.1" "&&"
    "./bin/fconfig.sh default device-name" "&&" "./bin/fconfig.sh list")
 gn-test-check-mock-args "${expected[@]}"

# shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.4"
  expected=("${ssh_common_expected[@]}" "ss -ln | grep :8083")
  gn-test-check-mock-args "${expected[@]}"

# shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.5"
  expected=("${ssh_common_expected[@]}" )
  expected+=(-6 -L "\*:8083:localhost:8083"
    -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22"
    -R "2345:[fe80::c0ff:eec0:ffee%coffee]:2345"
    -R "8443:[fe80::c0ff:eec0:ffee%coffee]:8443"
    -R "9080:[fe80::c0ff:eec0:ffee%coffee]:80"
    -o "ExitOnForwardFailure=yes" "cd" "\$HOME" "&&"
    "cd" "/home/path_to_samples/third_party/fuchsia-sdk" "&&" "./bin/fserve.sh" "&&" "sleep 0")
  gn-test-check-mock-args "${expected[@]}"


  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.6"
   expected=("${ssh_common_expected[@]}" -O "exit")
  gn-test-check-mock-args "${expected[@]}"

  BT_ASSERT_FILE_DOES_NOT_EXIST "${SSH_MOCK_PATH}/ssh.mock_state.7"
}

TEST_fserve_remote_with_config() {
  set_up_ssh
  set_up_device_finder

  REMOTE_PATH="/home/path_to_samples/third_party/fuchsia-sdk"

  BT_EXPECT "${FCONFIG_CMD}" set image "test-image"
  BT_EXPECT "${FCONFIG_CMD}" set bucket "custom-bucket"

  # Create a mock that simulates a clean environment, nothing is running.
  cat > "${SSH_MOCK_PATH}/ssh.mock_side_effects" <<"EOF"
    if [[ "$*" =~ SSH_CONNECTION ]]; then
      echo "172.20.100.10 38618 100.90.250.100 22"
      return 0
    elif [[ "$*" =~ "amber_ctl add_src" ]]; then
      return 0
    fi

    # No existing session.
    if [[ "$*" =~ "-O check" ]]; then
      return 255
    fi

    # No existing forwarding for 8022
    if [[ "$*" =~ "grep :8022" ]]; then
      return 2
    fi

    # No existing forwarding for 8083
    if [[ "$*" =~ "grep :8083" ]]; then
      return 2
    fi
    echo $@
EOF


  # Run command.
  BT_EXPECT "${FSERVE_REMOTE_CMD}" desktop.example.com "${REMOTE_PATH}" --ttl 0 > "${BT_TEMP_DIR}/fserve_remote_with_config_log.txt" 2>&1

  ssh_common_expected=(_ANY_  "desktop.example.com" )
  ssh_common_expected+=(-S "${HOME}/.ssh/control-fuchsia-fx-remote")
  ssh_common_expected+=(-o "ControlMaster=auto")
  ssh_common_expected+=(-t)

   # First SSH is to check if the mux session to the host exists
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
   expected=("${ssh_common_expected[@]}" -O check)
   gn-test-check-mock-args "${expected[@]}"

  # Look for an existing port forwarding for 8022
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected=("${ssh_common_expected[@]}" "ss -ln | grep :8022")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.3"
  expected=("${ssh_common_expected[@]}" "cd \$HOME" "&&"
    "cd /home/path_to_samples/third_party/fuchsia-sdk" "&&"
    "./bin/fconfig.sh set device-ip 127.0.0.1" "&&"
    "./bin/fconfig.sh default device-name" "&&"
    "./bin/fconfig.sh set bucket custom-bucket" "&&"
    "./bin/fconfig.sh set image test-image" "&&" "./bin/fconfig.sh list")
  gn-test-check-mock-args "${expected[@]}"

# shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.4"
  expected=("${ssh_common_expected[@]}" "ss -ln | grep :8083")
  gn-test-check-mock-args "${expected[@]}"

# shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.5"
  expected=("${ssh_common_expected[@]}" )
  expected+=(-6 -L "\*:8083:localhost:8083"
    -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22"
    -R "2345:[fe80::c0ff:eec0:ffee%coffee]:2345"
    -R "8443:[fe80::c0ff:eec0:ffee%coffee]:8443"
    -R "9080:[fe80::c0ff:eec0:ffee%coffee]:80" -o "ExitOnForwardFailure=yes"
    "cd" "\$HOME" "&&" "cd" "/home/path_to_samples/third_party/fuchsia-sdk"
    "&&" "./bin/fserve.sh" "&&" "sleep 0")
  gn-test-check-mock-args "${expected[@]}"


  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.6"
   expected=("${ssh_common_expected[@]}" -O "exit")
  gn-test-check-mock-args "${expected[@]}"

  BT_ASSERT_FILE_DOES_NOT_EXIST "${SSH_MOCK_PATH}/ssh.mock_state.7"
}

TEST_fserve_remote_existing_session() {
  set_up_ssh
  set_up_device_finder

  REMOTE_PATH="/home/path_to_samples/third_party/fuchsia-sdk"

# Create a mock that simulates fserve-remote is already running.
  cat > "${SSH_MOCK_PATH}/ssh.mock_side_effects" <<"EOF"
    if [[ "$*" =~ SSH_CONNECTION ]]; then
      echo "172.20.100.10 38618 100.90.250.100 22"
      return 0
    elif [[ "$*" =~ "amber_ctl add_src" ]]; then
      return 0
    fi

    # No existing session.
    if [[ "$*" =~ "-O check" ]]; then
      return 0
    fi

    # No existing forwarding for 8022
    if [[ "$*" =~ "grep :8022" ]]; then
      return 0
    fi

    # No existing forwarding for 8083
    if [[ "$*" =~ "grep :8083" ]]; then
      return 2
    fi

    # simulate kill working successfully.
    if [[ "$*" =~ "pkill -u \$USER sshd" ]]; then
      return 0
    fi
    echo $@
EOF

  # Run command.
  BT_EXPECT "${FSERVE_REMOTE_CMD}" desktop.example.com "${REMOTE_PATH}" --ttl 0 > "${BT_TEMP_DIR}/fserve_remote_log.txt" 2>&1

  # Common command line args, used for each call to ssh
  ssh_common_expected=(_ANY_  "desktop.example.com" )
  ssh_common_expected+=(-S "${HOME}/.ssh/control-fuchsia-fx-remote")
  ssh_common_expected+=(-o "ControlMaster=auto")
  ssh_common_expected+=(-t)

  # First SSH is to check if the mux session to the host exists
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected=("${ssh_common_expected[@]}" -O check)
  gn-test-check-mock-args "${expected[@]}"

  # Exit the existing the control session.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected=("${ssh_common_expected[@]}" -O "exit")
  gn-test-check-mock-args "${expected[@]}"

  # Check for an existing forwarding for port 8022.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.3"
  expected=("${ssh_common_expected[@]}" "ss -ln | grep :8022")
  gn-test-check-mock-args "${expected[@]}"

  # Kill sshd.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.4"
  expected=("${ssh_common_expected[@]}" "pkill -u \$USER sshd")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.5"
  expected=("${ssh_common_expected[@]}" "cd \$HOME" "&&"
    "cd /home/path_to_samples/third_party/fuchsia-sdk" "&&"
    "./bin/fconfig.sh set device-ip 127.0.0.1" "&&"
    "./bin/fconfig.sh default device-name" "&&" "./bin/fconfig.sh list")
 gn-test-check-mock-args "${expected[@]}"

# shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.6"
  expected=("${ssh_common_expected[@]}" "ss -ln | grep :8083")
  gn-test-check-mock-args "${expected[@]}"

# shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.7"
  expected=("${ssh_common_expected[@]}" )
  expected+=(-6 -L "\*:8083:localhost:8083"
    -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22"
    -R "2345:[fe80::c0ff:eec0:ffee%coffee]:2345"
    -R "8443:[fe80::c0ff:eec0:ffee%coffee]:8443"
    -R "9080:[fe80::c0ff:eec0:ffee%coffee]:80" -o "ExitOnForwardFailure=yes"
    "cd" "\$HOME" "&&" "cd" "/home/path_to_samples/third_party/fuchsia-sdk"
    "&&" "./bin/fserve.sh" "&&" "sleep 0")
  gn-test-check-mock-args "${expected[@]}"


  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.8"
   expected=("${ssh_common_expected[@]}" -O "exit")
  gn-test-check-mock-args "${expected[@]}"

  BT_ASSERT_FILE_DOES_NOT_EXIST "${SSH_MOCK_PATH}/ssh.mock_state.9"
}

TEST_fserve_remote_pm_running() {
  set_up_ssh
  set_up_device_finder

  REMOTE_PATH="/home/path_to_samples/third_party/fuchsia-sdk"

# Create a mock that simulates a clean environment, except pm is running on the remote.
  cat > "${SSH_MOCK_PATH}/ssh.mock_side_effects" <<"EOF"
    if [[ "$*" =~ SSH_CONNECTION ]]; then
      echo "172.20.100.10 38618 100.90.250.100 22"
      return 0
    elif [[ "$*" =~ "amber_ctl add_src" ]]; then
      return 0
    fi

    # No existing session.
    if [[ "$*" =~ "-O check" ]]; then
      return 255
    fi

    # No existing forwarding for 8022
    if [[ "$*" =~ "grep :8022" ]]; then
      return 2
    fi

    # No existing forwarding for 8083
    if [[ "$*" =~ "grep :8083" ]]; then
      return 0
    fi
    echo $@
EOF

  # Run command.
  BT_EXPECT "${FSERVE_REMOTE_CMD}" desktop.example.com "${REMOTE_PATH}" --ttl 0 > "${BT_TEMP_DIR}/fserve_remote_log.txt" 2>&1

  # Common command line args, used for each call to ssh
  ssh_common_expected=(_ANY_  "desktop.example.com" )
  ssh_common_expected+=(-S "${HOME}/.ssh/control-fuchsia-fx-remote")
  ssh_common_expected+=(-o "ControlMaster=auto")
  ssh_common_expected+=(-t)

  # First SSH is to check if the mux session to the host exists
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected=("${ssh_common_expected[@]}" -O check)
  gn-test-check-mock-args "${expected[@]}"


  # Check for an existing forwarding for port 8022.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected=("${ssh_common_expected[@]}" "ss -ln | grep :8022")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.3"
  expected=("${ssh_common_expected[@]}" "cd \$HOME" "&&"
    "cd /home/path_to_samples/third_party/fuchsia-sdk" "&&"
    "./bin/fconfig.sh set device-ip 127.0.0.1" "&&"
    "./bin/fconfig.sh default device-name"
    "&&" "./bin/fconfig.sh list")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.4"
  expected=("${ssh_common_expected[@]}" "ss -ln | grep :8083")
  gn-test-check-mock-args "${expected[@]}"

  # Check for killing existing pm server running
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.5"
  expected=("${ssh_common_expected[@]}" "pkill -u \$USER pm")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.6"
  expected=("${ssh_common_expected[@]}" )
  expected+=(-6 -L "\*:8083:localhost:8083"
    -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22"
    -R "2345:[fe80::c0ff:eec0:ffee%coffee]:2345"
    -R "8443:[fe80::c0ff:eec0:ffee%coffee]:8443"
    -R "9080:[fe80::c0ff:eec0:ffee%coffee]:80" -o "ExitOnForwardFailure=yes"
    "cd" "\$HOME" "&&" "cd" "/home/path_to_samples/third_party/fuchsia-sdk"
    "&&" "./bin/fserve.sh" "&&" "sleep 0")
  gn-test-check-mock-args "${expected[@]}"


  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.7"
  expected=("${ssh_common_expected[@]}" -O "exit")
  gn-test-check-mock-args "${expected[@]}"

  BT_ASSERT_FILE_DOES_NOT_EXIST "${SSH_MOCK_PATH}/ssh.mock_state.8"
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
  scripts/sdk/gn/base/tools/x64/device-finder
  scripts/sdk/gn/base/tools/arm64/device-finder
  scripts/sdk/gn/base/tools/x64/pm
  scripts/sdk/gn/base/tools/arm64/pm
  isolated_path_for/ssh
)

BT_INIT_TEMP_DIR() {
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta"

  # Create a stub SDK manifest.
  cp "${BT_DEPS_ROOT}/scripts/sdk/gn/testdata/meta/manifest.json" \
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta/manifest.json"
}

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"

  MOCKED_DEVICE_FINDER="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/device-finder"
}

BT_RUN_TESTS "$@"
