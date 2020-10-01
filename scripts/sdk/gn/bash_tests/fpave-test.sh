#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests that verify that fpave correctly reboots a Fuchsia device into Zedboot
# and starts up a bootserver for paving.

set -e

# Sets up an ssh mock binary on the $PATH of any subshells and creates a stub
# authorized_keys.
set_up_ssh() {
  export PATH="${BT_TEMP_DIR}/isolated_path_for:${PATH}"

  # This authorized_keys file must not be empty, but its contents aren't used.
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"
}

# Sets up a device-finder mock. The implemented mock aims to produce minimal
# output that parses correctly but is otherwise uninteresting.
set_up_device_finder() {
  cat >"${MOCKED_DEVICE_FINDER}.mock_side_effects" <<"EOF"
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
  *)
    last_arg="$1"
    ;;
  esac
  shift
done

if [[ "${last_arg}" == "custom-device-name" ]]; then
  echo "fe80:cccc:cccc:cccc%dev"
else
  echo "fe80::c0ff:eec0:ffee%coffee"
fi
EOF
}

# Sets up a gsutil mock. The implemented mock creates an empty gzipped tarball
# at the destination of the cp command.
set_up_gsutil() {
  cat >"${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/gsutil.mock_side_effects" <<"EOF"
if [[ "$1" != "cp" ]]; then
  # Ignore any invocations other than cp.
  exit 0
fi

outpath="$3"

cp "${BT_TEMP_DIR}/scripts/sdk/gn/testdata/empty.tar.gz" "${outpath}"
EOF
}

set_up_gsutil_multibucket() {
  cat > "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/gsutil.mock_side_effects" <<EOF
  if [[ "\$1" == "ls" ]]; then
    if [[ "\${2}" == *unknown.tgz ]]; then
      echo "ls: cannot access \'\${2}\': No such file or directory"
      exit 1
    elif [[ "\${2}" == gs://fuchsia/development/*image1.tgz ]]; then
      echo "\${2}"
    elif [[ "\${2}" == gs://fuchsia/development/*image2.tgz ]]; then
      echo "\${2}"
    elif [[ "\${2}" == gs://fuchsia/* ]]; then
      echo "gs://fuchsia/development/sdk_id/images/image1.tgz"
      echo "gs://fuchsia/development/sdk_id/images/image2.tgz"
      echo "gs://fuchsia/development/sdk_id/images/image3.tgz"
    elif [[ "\${2}" == gs://other/* ]]; then
      echo "gs://other/development/sdk_id/images/image4.tgz"
      echo "gs://other/development/sdk_id/images/image5.tgz"
      echo "gs://other/development/sdk_id/images/image6.tgz"
    fi
  elif [[ "\$1" == "cp" ]]; then
    outpath="\$3"
    mkdir -p "\$(dirname "\${outpath}")"
    cp ${BT_TEMP_DIR}/scripts/sdk/gn/testdata/empty.tar.gz "\${outpath}"
  fi
EOF
}

# Creates a stub Core SDK hashes file. The filename is based on the SDK version
# in manifest.json.
set_up_sdk_stubs() {
  local hash
  hash=$(run-md5 "${BT_TEMP_DIR}/scripts/sdk/gn/testdata/empty.tar.gz" | cut -d ' ' -f 1)

  # The filename is constructed from the Core SDK version ("id") in the
  # manifest. See //scripts/sdk/gn/testdata/meta/manifest.json.
  local tarball="${FUCHSIA_WORK_DIR}/8890373976687374912_generic-x64.tgz"
  echo "${hash} ${tarball}" >"${FUCHSIA_WORK_DIR}/image/image.md5"
}


# Verifies that the pave script correctly invokes ssh to restart the Fuchsia
# device.
TEST_fpave_restarts_device() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # Run command.
  BT_EXPECT gn-test-run-bash-script "${FPAVE_CMD}" \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"

  # Verify that the script attempted to reboot the device over SSH.
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/isolated_path_for/ssh.mock_state"

  local expected_args=( _ANY_ "-F" "${FUCHSIA_WORK_DIR}/sshconfig"
                       "fe80::c0ff:eec0:ffee%coffee" "dm" "reboot-recovery" )
  gn-test-check-mock-args "${expected_args[@]}"


  # Verify that ssh was only run once.
  BT_EXPECT_FILE_DOES_NOT_EXIST "${BT_TEMP_DIR}/isolated_path_for/ssh.mock_state.1"
}

# Verifies that the pave script correctly invokes the pave script from the
# Fuchsia core SDK.
TEST_fpave_starts_paving() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # Run command.
  BT_EXPECT gn-test-run-bash-script \
    "${FPAVE_CMD}" \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"

  # Verify that the pave.sh script from the Fuchsia SDK was started correctly.
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/image/pave.sh.mock_state"

  local expected_args=( _ANY_ --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" -1 )
  gn-test-check-mock-args "${expected_args[@]}"

  # Verify that pave.sh was only run once.
  BT_EXPECT_FILE_DOES_NOT_EXIST "${FUCHSIA_WORK_DIR}/image/pave.sh.mock_state.1"
}

# Verify that the tool fails if gsutil fails to find the specified image,
# and also doesn't list any available images
TEST_fpave_lists_notfound() {
  cat > "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/gsutil.mock_side_effects" <<"EOF"
  if [[ "$1" == "ls" ]]; then
    echo "ls: cannot access \'${2}\': No such file or directory"
    exit 1
  elif [[ "$1" == "cp" ]]; then
    echo "CommandException: No URLs matched: \'${2}\'"
    exit 1
  fi
EOF

  BT_EXPECT_FAIL gn-test-run-bash-script "${FPAVE_CMD}" --image unknown > list_images_output.txt  2>&1

  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "Image unknown not found"
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "Could not get list of available images for"
}

# Verify image names are listed if the image is not found.
TEST_fpave_lists_images() {
  set_up_gsutil_multibucket

  BT_EXPECT_FAIL gn-test-run-bash-script "${FPAVE_CMD}" --image unknown \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" > list_images_output.txt  2>&1
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "image1 image2 image3"

  BT_EXPECT_FAIL gn-test-run-bash-script "${FPAVE_CMD}" \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" \
    --bucket other --image unknown > list_images_output.txt 2>&1
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "image4 image5 image6 image1 image2 image3"
}

# Tests that paving the same image back to back does not re-unzip the image. Also tests that changing images
# causes the old image to be deleted before unpackaging.
TEST_fpave_switch_types() {
 set_up_ssh
 set_up_gsutil_multibucket
 set_up_device_finder

 BT_EXPECT "${FPAVE_CMD}" --prepare --image image1 --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" > pave_image1.txt 2>&1
 BT_EXPECT_FILE_CONTAINS pave_image1.txt "Paving image1 system image for SDK version 8890373976687374912 from gs://fuchsia/development/8890373976687374912/images/image1.tgz"
 BT_EXPECT_FILE_CONTAINS_SUBSTRING "${FUCHSIA_WORK_DIR}/image/image.md5" "8890373976687374912_image1.tgz"

 # Same command, should skip download
 BT_EXPECT "${FPAVE_CMD}" --prepare --image image1 --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" > pave_image1_again.txt 2>&1
 BT_EXPECT_FILE_CONTAINS_SUBSTRING pave_image1_again.txt "Skipping download, image exists."
 BT_EXPECT_FILE_CONTAINS_SUBSTRING "${FUCHSIA_WORK_DIR}/image/image.md5" "8890373976687374912_image1.tgz"

 # Switch images, should delete old file.
 BT_EXPECT "${FPAVE_CMD}" --prepare --image image2 --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" > pave_image2.txt 2>&1
 BT_EXPECT_FILE_CONTAINS_SUBSTRING pave_image2.txt "WARNING: Removing old image files."
 BT_EXPECT_FILE_CONTAINS_SUBSTRING "${FUCHSIA_WORK_DIR}/image/image.md5" "8890373976687374912_image2.tgz"
}
TEST_fpave_default_keys() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # Run command.
  BT_EXPECT gn-test-run-bash-script "${FPAVE_CMD}" "--image" "image1"

  # Verify that the pave.sh script from the Fuchsia SDK was started correctly.
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/image/pave.sh.mock_state"

  local expected_args=( _ANY_ --authorized-keys "${HOME}/.ssh/fuchsia_authorized_keys" -1 )
  gn-test-check-mock-args "${expected_args[@]}"

  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/isolated_path_for/ssh.mock_state"

  local expected_args=( _ANY_ "-F" "${FUCHSIA_WORK_DIR}/sshconfig"
                       "fe80::c0ff:eec0:ffee%coffee" "dm" "reboot-recovery" )
  gn-test-check-mock-args "${expected_args[@]}"
}

TEST_fpave_with_props() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  BT_EXPECT "${FCONFIG_CMD}" set device-name "custom-device-name"
  BT_EXPECT "${FCONFIG_CMD}" set bucket "other"
  BT_EXPECT "${FCONFIG_CMD}" set image "image4"

  BT_EXPECT "${FPAVE_CMD}" > "${BT_TEMP_DIR}/fpave_with_props_log.txt" 2>&1

  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/isolated_path_for/ssh.mock_state"
  expected_args=("${BT_TEMP_DIR}/isolated_path_for/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" fe80:cccc:cccc:cccc%dev "dm" reboot-recovery )
  gn-test-check-mock-args "${expected_args[@]}"

  BT_EXPECT_FILE_CONTAINS_SUBSTRING "${FUCHSIA_WORK_DIR}/image/image.md5" "8890373976687374912_image4.tgz"

}

TEST_fpave_in_zedboot() {
  # This test covers the case when the device is in zedboot, so ssh does not connect.

  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # mock ssh not connecting.
  echo 127 > "${BT_TEMP_DIR}/isolated_path_for/ssh.mock_status"

  # Run command.
  BT_EXPECT "${FPAVE_CMD}"  > "${BT_TEMP_DIR}/TEST_fpave_in_zedboot.log" 2>&1

  BT_EXPECT_FILE_CONTAINS_SUBSTRING "${BT_TEMP_DIR}/TEST_fpave_in_zedboot.log" \
      "WARNING: Confirm device is rebooting into recovery mode.  Paving may fail if device is not in Zedboot."

  # Verify that the pave.sh script from the Fuchsia SDK was started correctly.
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/image/pave.sh.mock_state"

  local expected_args=( _ANY_ --authorized-keys "${HOME}/.ssh/fuchsia_authorized_keys" -1 )
  gn-test-check-mock-args "${expected_args[@]}"

  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/isolated_path_for/ssh.mock_state"

  local expected_args=( _ANY_ "-F" "${FUCHSIA_WORK_DIR}/sshconfig"
                       "fe80::c0ff:eec0:ffee%coffee" "dm" "reboot-recovery" )
  gn-test-check-mock-args "${expected_args[@]}"
}

# shellcheck disable=SC2034
# Test initialization.
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fpave.sh
  scripts/sdk/gn/base/bin/fconfig.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/bin/gsutil
  scripts/sdk/gn/base/tools/x64/bootserver
  scripts/sdk/gn/base/tools/arm64/bootserver
  test-home/.fuchsia/image/pave.sh
  scripts/sdk/gn/base/tools/x64/device-finder
  scripts/sdk/gn/base/tools/arm64/device-finder
  isolated_path_for/ssh
)

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

  FPAVE_CMD="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fpave.sh"
  FCONFIG_CMD="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fconfig.sh"

  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"
  FUCHSIA_WORK_DIR="${HOME}/.fuchsia"

  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/testdata"
  tar czf "${BT_TEMP_DIR}/scripts/sdk/gn/testdata/empty.tar.gz" -C "${FUCHSIA_WORK_DIR}/image"  "."

  MOCKED_DEVICE_FINDER="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/device-finder"

}

BT_INIT_TEMP_DIR() {
  mkdir -p \
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta" \
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata" \
    "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/data"

  # Create a stub SDK manifest.
  cp "${BT_DEPS_ROOT}/scripts/sdk/gn/testdata/meta/manifest.json" \
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta/manifest.json"
}

BT_RUN_TESTS "$@"
