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
    #
    # DO NOT RELY ON mock_state.n files for this SSH.
    #
    # There are some calls to SSH that are done in the background,
    # The mock implementation does not handle these concurrent calls,
    # so we write out our own state file to capture the parameters.
    # The names of the state files are hard coded, so if you need
    # a new call to ssh, you'll need to add the file.
    #
    source  "$(dirname $0)/../scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

    if [[ "$*" =~ SSH_CONNECTION ]]; then
      echo "172.20.100.10 38618 100.90.250.100 22"
      return 0
    elif [[ "$*" =~ "amber_ctl add_src" ]]; then
      return 0
    fi

    # if the check is in quiet mode, return true if there is a tunnel
    # file, indicating that the mock ssh call that creates the tunnel
    # has finished. Since it happens in the background, this check avoids
    # racing conditions and flake tests.
    if [[ "$*" =~ "-q -O check" ]]; then
      rc=0
      if [[ ! -f "$0.tunnel" ]]; then
        rc=254
      fi
      gn-test-log-mock "${0}.qcheck" $rc $@
      return $rc
    fi

    # No existing session.
    if [[ "$*" =~ "-O check" ]]; then
      rc=255
      gn-test-log-mock "${0}.check" $rc $@
      return "${rc}"
    fi

    # No existing forwarding for 8022
    if [[ "$*" =~ "grep :8022" ]]; then
      rc=255
      gn-test-log-mock "${0}.check_for_8022" $rc $@
      return "${rc}"
    fi


    # No existing forwarding for 8083
    if [[ "$*" =~ "grep :8083" ]]; then
      rc=2
      gn-test-log-mock "${0}.check_for_8083" $rc $@
      return "${rc}"
    fi

    # log the call to make the tunnel
    if [[ "$*" =~ "-6 -L" ]]; then
      rc=0
      gn-test-log-mock "${0}.tunnel" $rc $@
      return "${rc}"
    fi

    # log the call to configure the settings
    if [[ "$*" =~ "fconfig.sh list" ]]; then
      rc=0
      gn-test-log-mock "${0}.fconfig" $rc $@
      return "${rc}"
    fi
    # log the call to configure the settings
    if [[ "$*" =~ "fserve.sh" ]]; then
      rc=0
      gn-test-log-mock "${0}.fserve" $rc $@
      return "${rc}"
    fi
    echo $@
EOF

  # Run command.
  BT_EXPECT "${FSERVE_REMOTE_CMD}" desktop.example.com "${REMOTE_PATH}" --image image1 --ttl 0 > "${BT_TEMP_DIR}/fserve_remote_log.txt" 2>&1

  # Common command line args, used for each call to ssh
  ssh_common_expected=(_ANY_  "desktop.example.com" )
  ssh_common_expected+=(-S "${HOME}/.ssh/control-fuchsia-fx-remote")
  ssh_common_expected+=(-o "ControlMaster=auto")
  ssh_common_expected+=(-t)

  # First SSH is to check if the mux session to the host exists
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check.1"
  expected=("${ssh_common_expected[@]}" -O check)
  gn-test-check-mock-args "${expected[@]}"

  # Check for an existing forwarding for port 8022.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check_for_8022"
  expected=("${ssh_common_expected[@]}" "ss" "-ln" "|" "grep" ":8022")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  # A loop in fserve-remote checks every second if the tunnel has been
  # established, every time invocating the ssh mock to check it and thus
  # creating a new mock_state file for ssh.qcheck. The gn-test-latest-mock
  # function guarantees that we only check the latest invocation of the mock.
  source "$(gn-test-latest-mock "${SSH_MOCK_PATH}/ssh.qcheck")"
  expected=("${ssh_common_expected[@]}" -q -O check)
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.fconfig"
  expected=("${ssh_common_expected[@]}" "cd" "\$HOME" "&&"
    "cd" "/home/path_to_samples/third_party/fuchsia-sdk" "&&"
    "./bin/fconfig.sh" "set" "device-ip" "127.0.0.1" "&&"
    "./bin/fconfig.sh" "default" "device-name" "&&"
    "./bin/fconfig.sh" "set" "image" "image1" "&&"
    "./bin/fconfig.sh" "list" )
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check_for_8083"
  expected=("${ssh_common_expected[@]}" "ss" "-ln" "|" "grep" ":8083")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.fserve"
  expected=("${ssh_common_expected[@]}" )
  expected+=("cd" "\$HOME" "&&"
    "cd" "/home/path_to_samples/third_party/fuchsia-sdk" "&&" "./bin/fserve.sh" "&&" "sleep" "0")

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.tunnel"
  expected=("${ssh_common_expected[@]}" )
  expected+=(-6 -L "\*:8083:localhost:8083"
    -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22"
    -R "2345:[fe80::c0ff:eec0:ffee%coffee]:2345"
    -R "8443:[fe80::c0ff:eec0:ffee%coffee]:8443"
    -R "9080:[fe80::c0ff:eec0:ffee%coffee]:80"
    -o "ExitOnForwardFailure=yes" "-nT" "sleep" "0")
  gn-test-check-mock-args "${expected[@]}"
}

TEST_fserve_remote_with_config() {
  set_up_ssh
  set_up_device_finder

  REMOTE_PATH="/home/path_to_samples/third_party/fuchsia-sdk"

  BT_EXPECT "${FCONFIG_CMD}" set image "test-image"
  BT_EXPECT "${FCONFIG_CMD}" set bucket "custom-bucket"

  # Create a mock that simulates a clean environment, nothing is running.
  cat > "${SSH_MOCK_PATH}/ssh.mock_side_effects" <<"EOF"
    #
    # DO NOT RELY ON mock_state.n files for this SSH.
    #
    # There are some calls to SSH that are done in the background,
    # The mock implementation does not handle these concurrent calls,
    # so we write out our own state file to capture the parameters.
    # The names of the state files are hard coded, so if you need
    # a new call to ssh, you'll need to add the file.
    #
    source  "$(dirname $0)/../scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

    if [[ "$*" =~ SSH_CONNECTION ]]; then
      echo "172.20.100.10 38618 100.90.250.100 22"
      return 0
    elif [[ "$*" =~ "amber_ctl add_src" ]]; then
      return 0
    fi

    # if the check is in quiet mode, return true if there is a tunnel
    # file, indicating that the mock ssh call that creates the tunnel
    # has finished. Since it happens in the background, this check avoids
    # racing conditions and flake tests.
    if [[ "$*" =~ "-q -O check" ]]; then
      rc=0
      if [[ ! -f "$0.tunnel" ]]; then
        rc=254
      fi
      gn-test-log-mock "${0}.qcheck" $rc $@
      return $rc
    fi

    # No existing session.
    if [[ "$*" =~ "-O check" ]]; then
      rc=255
      gn-test-log-mock "${0}.check" $rc $@
      return $rc
    fi

    # No existing forwarding for 8022
    if [[ "$*" =~ "grep :8022" ]]; then
      rc=2
      gn-test-log-mock "${0}.check_for_8022" $rc $@
      return "${rc}"
    fi

    # No existing forwarding for 8083
    if [[ "$*" =~ "grep :8083" ]]; then
      rc=2
      gn-test-log-mock "${0}.check_for_8083" $rc $@
      return "${rc}"
    fi

    # log the call to make the tunnel
    if [[ "$*" =~ "-6 -L" ]]; then
      rc=0
      gn-test-log-mock "${0}.tunnel" $rc $@
      return "${rc}"
    fi

    # log the call to configure the settings
    if [[ "$*" =~ "fconfig.sh list" ]]; then
      rc=0
      gn-test-log-mock "${0}.fconfig" $rc $@
      return "${rc}"
    fi
    # log the call to configure the settings
    if [[ "$*" =~ "fserve.sh" ]]; then
      rc=0
      gn-test-log-mock "${0}.fserve" $rc $@
      return "${rc}"
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
  source "${SSH_MOCK_PATH}/ssh.check.1"
  expected=("${ssh_common_expected[@]}" -O check)
  gn-test-check-mock-args "${expected[@]}"

  # Look for an existing port forwarding for 8022
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check_for_8022"
  expected=("${ssh_common_expected[@]}" "ss" "-ln" "|" "grep" ":8022")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  # A loop in fserve-remote checks every second if the tunnel has been
  # established, every time invocating the ssh mock to check it and thus
  # creating a new mock_state file for ssh.qcheck. The gn-test-latest-mock
  # function guarantees that we only check the latest invocation of the mock.
  source "$(gn-test-latest-mock "${SSH_MOCK_PATH}/ssh.qcheck")"
  expected=("${ssh_common_expected[@]}" -q -O check)
  gn-test-check-mock-args "${expected[@]}"


  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.fconfig"
  expected=("${ssh_common_expected[@]}" "cd" "\$HOME" "&&"
    "cd" "/home/path_to_samples/third_party/fuchsia-sdk" "&&"
    "./bin/fconfig.sh" "set" "device-ip" "127.0.0.1" "&&"
    "./bin/fconfig.sh" "default" "device-name" "&&"
    "./bin/fconfig.sh" "set" "bucket" "custom-bucket" "&&"
    "./bin/fconfig.sh" "set" "image" "test-image" "&&"
    "./bin/fconfig.sh" "list")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check_for_8083"
  expected=("${ssh_common_expected[@]}" "ss" "-ln" "|" "grep" ":8083")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.fserve"
  expected=("${ssh_common_expected[@]}" )
  expected+=("cd" "\$HOME" "&&" "cd" "/home/path_to_samples/third_party/fuchsia-sdk"
    "&&" "./bin/fserve.sh" "&&" "sleep" "0")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check.2"
  expected=("${ssh_common_expected[@]}" -O "check")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.tunnel"
  expected=("${ssh_common_expected[@]}" )
  expected+=(-6 -L "\*:8083:localhost:8083"
    -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22"
    -R "2345:[fe80::c0ff:eec0:ffee%coffee]:2345"
    -R "8443:[fe80::c0ff:eec0:ffee%coffee]:8443"
    -R "9080:[fe80::c0ff:eec0:ffee%coffee]:80"
    -o "ExitOnForwardFailure=yes" "-nT" "sleep" "0")
  gn-test-check-mock-args "${expected[@]}"
}

TEST_fserve_remote_existing_session() {
  set_up_ssh
  set_up_device_finder

  REMOTE_PATH="/home/path_to_samples/third_party/fuchsia-sdk"

  # Create a mock that simulates fserve-remote is already running.
  cat > "${SSH_MOCK_PATH}/ssh.mock_side_effects" <<"EOF"
    #
    # DO NOT RELY ON mock_state.n files for this SSH.
    #
    # There are some calls to SSH that are done in the background,
    # The mock implementation does not handle these concurrent calls,
    # so we write out our own state file to capture the parameters.
    # The names of the state files are hard coded, so if you need
    # a new call to ssh, you'll need to add the file.
    #
    source  "$(dirname $0)/../scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

    if [[ "$*" =~ SSH_CONNECTION ]]; then
      echo "172.20.100.10 38618 100.90.250.100 22"
      return 0
    elif [[ "$*" =~ "amber_ctl add_src" ]]; then
      return 0
    fi

    # if the check is in quiet mode, return true if there is a tunnel
    # file, indicating that the mock ssh call that creates the tunnel
    # has finished. Since it happens in the background, this check avoids
    # racing conditions and flake tests.
    if [[ "$*" =~ "-q -O check" ]]; then
      rc=0
      if [[ ! -f "$0.tunnel" ]]; then
        rc=254
      fi
      gn-test-log-mock "${0}.qcheck" $rc $@
      return $rc
    fi

    # existing session return 0.
    if [[ "$*" =~ "-O check" ]]; then
      rc=0
      gn-test-log-mock "${0}.check" $rc $@
      return $rc
    fi

    # existing forwarding for 8022
    if [[ "$*" =~ "grep :8022" ]]; then
      rc=0
      gn-test-log-mock "${0}.check_for_8022" $rc $@
      return "${rc}"
    fi

    # No existing forwarding for 8083
    if [[ "$*" =~ "grep :8083" ]]; then
      rc=2
      gn-test-log-mock "${0}.check_for_8083" $rc $@
      return "${rc}"
    fi

    # simulate kill working successfully.
    if [[ "$*" =~ "pkill -u \$USER sshd" ]]; then
      rc=0
      gn-test-log-mock "${0}.pkill" $rc $@
      return "${rc}"
    fi

    # log the call to make the tunnel
    if [[ "$*" =~ "-6 -L" ]]; then
      rc=0
      gn-test-log-mock "${0}.tunnel" $rc $@
      return "${rc}"
    fi

    # log the call to configure the settings
    if [[ "$*" =~ "fconfig.sh list" ]]; then
      rc=0
      gn-test-log-mock "${0}.fconfig" $rc $@
      return "${rc}"
    fi
    # log the call to configure the settings
    if [[ "$*" =~ "fserve.sh" ]]; then
      rc=0
      gn-test-log-mock "${0}.fserve" $rc $@
      return "${rc}"
    fi
    # log the call to exit
    if [[ "$*" =~ "-O exit" ]]; then
      rc=0
      gn-test-log-mock "${0}.exit" $rc $@
      return "${rc}"
    fi
     echo $@
EOF

  # Run command.
  BT_EXPECT "${FSERVE_REMOTE_CMD}" desktop.example.com "${REMOTE_PATH}" --image image1 --ttl 0 > "${BT_TEMP_DIR}/fserve_remote_log.txt" 2>&1

  # Common command line args, used for each call to ssh
  ssh_common_expected=(_ANY_  "desktop.example.com" )
  ssh_common_expected+=(-S "${HOME}/.ssh/control-fuchsia-fx-remote")
  ssh_common_expected+=(-o "ControlMaster=auto")
  ssh_common_expected+=(-t)

  # First SSH is to check if the mux session to the host exists
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check.1"
  expected=("${ssh_common_expected[@]}" -O check)
  gn-test-check-mock-args "${expected[@]}"

  # There is another check before calling exit.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check.2"
  expected=("${ssh_common_expected[@]}" -O "check")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.exit.1"
  expected=("${ssh_common_expected[@]}" -O "exit")
  gn-test-check-mock-args "${expected[@]}"

  # Check for an existing forwarding for port 8022.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check_for_8022"
  expected=("${ssh_common_expected[@]}" "ss" "-ln" "|" "grep" ":8022")
  gn-test-check-mock-args "${expected[@]}"

  # Kill sshd.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.pkill"
  expected=("${ssh_common_expected[@]}" "pkill" "-u" "\$USER" "sshd")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  # A loop in fserve-remote checks every second if the tunnel has been
  # established, every time invocating the ssh mock to check it and thus
  # creating a new mock_state file for ssh.qcheck. The gn-test-latest-mock
  # function guarantees that we only check the latest invocation of the mock.
  source "$(gn-test-latest-mock "${SSH_MOCK_PATH}/ssh.qcheck")"
  expected=("${ssh_common_expected[@]}" -q -O check)
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.fconfig"
  expected=("${ssh_common_expected[@]}" "cd" "\$HOME" "&&"
    "cd" "/home/path_to_samples/third_party/fuchsia-sdk" "&&"
    "./bin/fconfig.sh" "set" "device-ip" "127.0.0.1" "&&"
    "./bin/fconfig.sh" "default" "device-name" "&&"
    "./bin/fconfig.sh" "set" "image" "image1" "&&"
    "./bin/fconfig.sh" "list")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check_for_8083"
  expected=("${ssh_common_expected[@]}" "ss" "-ln" "|" "grep" ":8083")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.fserve"
  expected=("${ssh_common_expected[@]}" )
  expected+=("cd" "\$HOME" "&&" "cd" "/home/path_to_samples/third_party/fuchsia-sdk"
    "&&" "./bin/fserve.sh" "&&" "sleep" "0")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check.3"
  expected=("${ssh_common_expected[@]}" -O "check")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.exit.2"
  expected=("${ssh_common_expected[@]}" -O "exit")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.tunnel"
  expected=("${ssh_common_expected[@]}" )
  expected+=(-6 -L "\*:8083:localhost:8083"
    -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22"
    -R "2345:[fe80::c0ff:eec0:ffee%coffee]:2345"
    -R "8443:[fe80::c0ff:eec0:ffee%coffee]:8443"
    -R "9080:[fe80::c0ff:eec0:ffee%coffee]:80"
    -o "ExitOnForwardFailure=yes" "-nT" "sleep" "0")
  gn-test-check-mock-args "${expected[@]}"
}

TEST_fserve_remote_pm_running() {
  set_up_ssh
  set_up_device_finder

  REMOTE_PATH="/home/path_to_samples/third_party/fuchsia-sdk"

  # Create a mock that simulates a clean environment, except pm is running on the remote.
  cat > "${SSH_MOCK_PATH}/ssh.mock_side_effects" <<"EOF"
    #
    # DO NOT RELY ON mock_state.n files for this SSH.
    #
    # There are some calls to SSH that are done in the background,
    # The mock implementation does not handle these concurrent calls,
    # so we write out our own state file to capture the parameters.
    # The names of the state files are hard coded, so if you need
    # a new call to ssh, you'll need to add the file.
    #
    source  "$(dirname $0)/../scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

    if [[ "$*" =~ SSH_CONNECTION ]]; then
      echo "172.20.100.10 38618 100.90.250.100 22"
      return 0
    elif [[ "$*" =~ "amber_ctl add_src" ]]; then
      return 0
    fi

    # if the check is in quiet mode, return true if there is a tunnel
    # file, indicating that the mock ssh call that creates the tunnel
    # has finished. Since it happens in the background, this check avoids
    # racing conditions and flake tests.
    if [[ "$*" =~ "-q -O check" ]]; then
      rc=0
      if [[ ! -f "$0.tunnel" ]]; then
        rc=254
      fi
      gn-test-log-mock "${0}.qcheck" $rc $@
      return $rc
    fi

     # No existing session.
    if [[ "$*" =~ "-O check" ]]; then
      rc=255
      gn-test-log-mock "${0}.check" $rc $@
      return $rc
    fi

    # No existing forwarding for 8022
    if [[ "$*" =~ "grep :8022" ]]; then
      rc=2
      gn-test-log-mock "${0}.check_for_8022" $rc $@
      return "${rc}"
    fi

    # Existing forwarding for 8083
    if [[ "$*" =~ "grep :8083" ]]; then
      rc=0
      gn-test-log-mock "${0}.check_for_8083" $rc $@
      return "${rc}"
    fi

    # simulate kill working successfully.
    if [[ "$*" =~ "pkill -u \$USER" ]]; then
      rc=0
      gn-test-log-mock "${0}.pkill" $rc $@
      return "${rc}"
    fi

    # log the call to make the tunnel
    if [[ "$*" =~ "-6 -L" ]]; then
      rc=0
      gn-test-log-mock "${0}.tunnel" $rc $@
      return "${rc}"
    fi

    # log the call to configure the settings
    if [[ "$*" =~ "fconfig.sh list" ]]; then
      rc=0
      gn-test-log-mock "${0}.fconfig" $rc $@
      return "${rc}"
    fi
    # log the call to configure the settings
    if [[ "$*" =~ "fserve.sh" ]]; then
      rc=0
      gn-test-log-mock "${0}.fserve" $rc $@
      return "${rc}"
    fi
    # log the call to exit
    if [[ "$*" =~ "-O exit" ]]; then
      rc=0
      gn-test-log-mock "${0}.exit" $rc $@
      return "${rc}"
    fi
    echo $@
EOF

  # Run command.
  BT_EXPECT "${FSERVE_REMOTE_CMD}" desktop.example.com "${REMOTE_PATH}" --image image1 --ttl 0 > "${BT_TEMP_DIR}/fserve_remote_log.txt" 2>&1

  # Common command line args, used for each call to ssh
  ssh_common_expected=(_ANY_  "desktop.example.com" )
  ssh_common_expected+=(-S "${HOME}/.ssh/control-fuchsia-fx-remote")
  ssh_common_expected+=(-o "ControlMaster=auto")
  ssh_common_expected+=(-t)

  # First SSH is to check if the mux session to the host exists
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check.1"
  expected=("${ssh_common_expected[@]}" "-O" "check")
  gn-test-check-mock-args "${expected[@]}"


  # Check for an existing forwarding for port 8022.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check_for_8022"
  expected=("${ssh_common_expected[@]}" "ss" "-ln" "|" "grep" ":8022")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  # A loop in fserve-remote checks every second if the tunnel has been
  # established, every time invocating the ssh mock to check it and thus
  # creating a new mock_state file for ssh.qcheck. The gn-test-latest-mock
  # function guarantees that we only check the latest invocation of the mock.
  source "$(gn-test-latest-mock "${SSH_MOCK_PATH}/ssh.qcheck")"
  expected=("${ssh_common_expected[@]}" -q -O check)
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.fconfig"
  expected=("${ssh_common_expected[@]}" "cd" "\$HOME" "&&"
    "cd" "/home/path_to_samples/third_party/fuchsia-sdk" "&&"
    "./bin/fconfig.sh" "set" "device-ip" "127.0.0.1" "&&"
    "./bin/fconfig.sh" "default" "device-name" "&&"
    "./bin/fconfig.sh" "set" "image" "image1" "&&"
    "./bin/fconfig.sh" "list")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check_for_8083"
  expected=("${ssh_common_expected[@]}" "ss" "-ln" "|" "grep" ":8083")
  gn-test-check-mock-args "${expected[@]}"

  # Check for killing existing pm server running
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.pkill"
  expected=("${ssh_common_expected[@]}" "pkill" "-u" "\$USER" "pm")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.fserve"
  expected=("${ssh_common_expected[@]}" )
  expected+=("cd" "\$HOME" "&&" "cd" "/home/path_to_samples/third_party/fuchsia-sdk"
    "&&" "./bin/fserve.sh" "&&" "sleep" "0")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.check.2"
  expected=("${ssh_common_expected[@]}" -O "check")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.tunnel"
  expected=("${ssh_common_expected[@]}" )
  expected+=(-6 -L "\*:8083:localhost:8083"
    -R "8022:[fe80::c0ff:eec0:ffee%coffee]:22"
    -R "2345:[fe80::c0ff:eec0:ffee%coffee]:2345"
    -R "8443:[fe80::c0ff:eec0:ffee%coffee]:8443"
    -R "9080:[fe80::c0ff:eec0:ffee%coffee]:80"
    -o "ExitOnForwardFailure=yes" "-nT" "sleep" "0")
  gn-test-check-mock-args "${expected[@]}"
}

# Test initialization. Note that we copy various tools/devshell files and need to replicate the
# behavior of generate.py by copying these files into scripts/sdk/gn/base/bin/devshell
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fconfig.sh
  scripts/sdk/gn/base/bin/fserve-remote.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
  tools/devshell/tests/subcommands/data/fx_remote_test/verify-default-keys.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/bin/gsutil
  scripts/sdk/gn/base/tools/x64/device-finder
  scripts/sdk/gn/base/tools/arm64/device-finder
  scripts/sdk/gn/base/tools/x64/pm
  scripts/sdk/gn/base/tools/arm64/pm
  isolated_path_for/ssh
  isolated_path_for/kill
)

BT_INIT_TEMP_DIR() {
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta"

  # Create a stub SDK manifest.
  cp "${BT_DEPS_ROOT}/scripts/sdk/gn/testdata/meta/manifest.json" \
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta/manifest.json"

  # Stage the files we copy from fx implementation, replicating behavior of generate.py
  cp -r "${BT_TEMP_DIR}/tools/devshell" "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/"

  mkdir "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib"
  mv "${BT_TEMP_DIR}/tools/devshell/tests/subcommands/data/fx_remote_test/verify-default-keys.sh" "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/verify-default-keys.sh"
}

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"

  MOCKED_DEVICE_FINDER="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/device-finder"

  if [[ "$(type -t kill)" == "builtin" ]]; then
    kill() {
      "${BT_TEMP_DIR}/isolated_path_for/kill" "$@"
    }
  fi
  # Create mock "kill" command$.
  # Need to embed "enable -n kill" into fuchsia-common.sh to disable the
  # bash builtin kill so we can intercept it.
  echo "enable -n kill" >> "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fuchsia-common.sh"
}

BT_RUN_TESTS "$@"
