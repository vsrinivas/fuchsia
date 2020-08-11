#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test that verifies that fserve starts up a package server. This test also
# verifies that fserve connects to a connected Fuchsia device and registers the
# package server with the device.

set -e

FSERVE_CMD="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fserve.sh"
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
  cat >"${MOCKED_DEVICE_FINDER}.mock_side_effects" <<"SETVAR"
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
SETVAR
}

# Sets up a gsutil mock. The implemented mock creates an empty gzipped tarball
# at the destination of the cp command.
set_up_gsutil() {
  cat >"${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/gsutil.mock_side_effects" <<"SETVAR"

if [[ "$1" != "cp" ]]; then
  # Ignore any invocations other than cp.
  exit 0
fi

outpath="$3"

mkdir -p "$(dirname "${outpath}")"
touch foo
tar czf "${outpath}" foo
SETVAR
}

set_up_gsutil_multibucket() {
  cat > "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/gsutil.mock_side_effects" <<"EOF"
  if [[ "$1" == "ls" ]]; then
    if [[ "${2}" == *unknown.tar.gz ]]; then
      echo "ls: cannot access \'${2}\': No such file or directory"
      exit 1
    elif [[ "${2}" == gs://fuchsia/* ]]; then
      echo "gs://fuchsia/development/sdk_id/images/image1.tgz"
      echo "gs://fuchsia/development/sdk_id/images/image2.tgz"
      echo "gs://fuchsia/development/sdk_id/images/image3.tgz"
    elif [[ "${2}" == gs://other/* ]]; then
      echo "gs://other/development/sdk_id/images/image4.tgz"
      echo "gs://other/development/sdk_id/images/image5.tgz"
      echo "gs://other/development/sdk_id/images/image6.tgz"
    fi
  elif [[ "$1" == "cp" ]]; then
    outpath="$3"
    mkdir -p "$(dirname "${outpath}")"
    cp ../testdata/empty.tar.gz "${outpath}"
  fi
EOF
}

# Creates a stub Core SDK hashes file. The filename is based on the SDK version
# in manifest.json.
set_up_sdk_stubs() {
  # The filename is constructed from the Core SDK version ("id") in the
  # manifest. See //scripts/sdk/gn/testdata/meta/manifest.json.
  mkdir -p "${FUCHSIA_WORK_DIR}/image"
  local tarball="${FUCHSIA_WORK_DIR}/8890373976687374912_generic-x64.tgz"

  touch foo
  tar czf "${tarball}" foo

  local hash
  hash=$(run-md5 "${tarball}" | cut -d ' ' -f 1)
  echo "${hash}  ${tarball}" >"${FUCHSIA_WORK_DIR}/image/image.md5"
}

# Verify that pm serve was run correctly.
TEST_fserve_starts_package_server() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # Run command.
  BT_EXPECT "${FSERVE_CMD}" --image generic-x64 > "${BT_TEMP_DIR}/fserve_starts_package_server.txt" 2>&1

  # shellcheck disable=SC1090
  source "${MOCKED_PM}.mock_state"

  expected=("${MOCKED_PM}" serve -repo "${FUCHSIA_WORK_DIR}/packages/amber-files" -l ":8083" )
  gn-test-check-mock-args "${expected[@]}"

  # Verify that pm was only run once.
  BT_EXPECT_FILE_DOES_NOT_EXIST "${MOCKED_PM}.mock_state.1"
}

# Verifies that the package server has been correctly registered with the
# Fuchsia device.
TEST_fserve_registers_package_server() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # Run command.
  BT_EXPECT "${FSERVE_CMD}" --image generic-x64 > "${BT_TEMP_DIR}/fserve_registers_package_server.txt" 2>&1

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected=(_ANY_ -F "${FUCHSIA_WORK_DIR}/sshconfig" "fe80::c0ff:eec0:ffee%coffee" "echo" "\$SSH_CONNECTION")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"

  # The host address matches the mock in ssh
  expected=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" "fe80::c0ff:eec0:ffee%coffee" amber_ctl add_src -f "http://172.20.100.10:8083/config.json" )
  gn-test-check-mock-args "${expected[@]}"
}

# Verify that the tool fails if gsutil fails to find the specified image,
# and also doesn't list any available images
TEST_fserve_lists_notfound() {
  cat > "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/gsutil.mock_side_effects" <<"EOF"
  if [[ "$1" == "ls" ]]; then
    echo "ls: cannot access \'${2}\': No such file or directory"
    exit 1
  elif [[ "$1" == "cp" ]]; then
    echo "CommandException: No URLs matched: \'${2}\'"
    exit 1
  fi
EOF

  BT_EXPECT_FAIL gn-test-run-bash-script "${FSERVE_CMD}" --image unknown > list_images_output.txt  2>&1

  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "Packages for unknown not found"
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "Could not get list of available images for"
}

# Verify image names are listed if the image is not found.
TEST_fserve_lists_images() {
  set_up_gsutil_multibucket

  BT_EXPECT_FAIL "${FSERVE_CMD}" --image unknown > list_images_output.txt  2>&1
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "image1 image2 image3"

  BT_EXPECT_FAIL "${FSERVE_CMD}" --bucket other --image unknown > list_images_output.txt 2>&1
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "image4 image5 image6 image1 image2 image3"
}

TEST_fserve_device_name(){
  set_up_ssh
  set_up_device_finder
  set_up_gsutil

  BT_EXPECT "${FSERVE_CMD}" --device-name coffee-coffee-coffee-coffee > /dev/null 2>&1


  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected=(_ANY_ -F "${FUCHSIA_WORK_DIR}/sshconfig" "fe80::c0ff:eec0:ffee%coffee" "echo" "\$SSH_CONNECTION")
  gn-test-check-mock-args "${expected[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" "fe80::c0ff:eec0:ffee%coffee" _ANY_ _ANY_ _ANY_ _ANY_)
  gn-test-check-mock-args "${expected_args[@]}"
}


TEST_fserve_device_addr(){
  set_up_ssh
  set_up_device_finder
  set_up_gsutil

  BT_EXPECT "${FSERVE_CMD}" --device-ip 192.1.1.1 > "${BT_TEMP_DIR}/fserve_device_addr_log.txt" 2>&1

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" 192.1.1.1 "echo" _ANY_ )
  gn-test-check-mock-args "${expected_args[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" 192.1.1.1  amber_ctl _ANY_ _ANY_ _ANY_)
  gn-test-check-mock-args "${expected_args[@]}"
  }

TEST_fserve_with_ip_prop() {
   set_up_ssh
  set_up_device_finder
  set_up_gsutil

  BT_EXPECT "${FCONFIG_CMD}" set device-ip "192.1.1.2"

  BT_EXPECT "${FSERVE_CMD}" > "${BT_TEMP_DIR}/fserve_with_props_log.txt" 2>&1

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" 192.1.1.2 "echo" _ANY_ )
  gn-test-check-mock-args "${expected_args[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" 192.1.1.2  amber_ctl _ANY_ _ANY_ _ANY_)
  gn-test-check-mock-args "${expected_args[@]}"
}

TEST_fserve_with_name_prop() {
   set_up_ssh
  set_up_device_finder
  set_up_gsutil

  BT_EXPECT "${FCONFIG_CMD}" set device-name "coffee-coffee-coffee-coffee"

  BT_EXPECT "${FSERVE_CMD}" > "${BT_TEMP_DIR}/fserve_with_props_log.txt" 2>&1

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" "fe80::c0ff:eec0:ffee%coffee" "echo" _ANY_ )
  gn-test-check-mock-args "${expected_args[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" "fe80::c0ff:eec0:ffee%coffee"  amber_ctl _ANY_ _ANY_ _ANY_)
  gn-test-check-mock-args "${expected_args[@]}"
}

TEST_fserve_with_all_props() {
   set_up_ssh
  set_up_device_finder
  set_up_gsutil

  BT_EXPECT "${FCONFIG_CMD}" set device-ip "192.1.1.2"
  BT_EXPECT "${FCONFIG_CMD}" set device-name "coffee-coffee-coffee-coffee"

  BT_EXPECT "${FSERVE_CMD}" > "${BT_TEMP_DIR}/fserve_with_props_log.txt" 2>&1

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.1"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" 192.1.1.2 "echo" _ANY_ )
  gn-test-check-mock-args "${expected_args[@]}"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state.2"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" 192.1.1.2  amber_ctl _ANY_ _ANY_ _ANY_)
  gn-test-check-mock-args "${expected_args[@]}"
}

# Test initialization.
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fserve.sh
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

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"
  FUCHSIA_WORK_DIR="${HOME}/.fuchsia"

  MOCKED_DEVICE_FINDER="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/device-finder"
  MOCKED_PM="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/pm"
}

BT_INIT_TEMP_DIR() {
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta"

  # Create a stub SDK manifest.
  cp "${BT_DEPS_ROOT}/scripts/sdk/gn/testdata/meta/manifest.json" \
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta/manifest.json"
}

BT_RUN_TESTS "$@"
