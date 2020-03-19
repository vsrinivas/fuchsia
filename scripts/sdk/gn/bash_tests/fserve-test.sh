#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test that verifies that fserve starts up a package server. This test also
# verifies that fserve connects to a connected Fuchsia device and registers the
# package server with the device.

set -e
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/gn-bash-test-lib.sh"

FSERVE_CMD="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fserve.sh"

# Sets up an ssh mock binary on the $PATH of any subshells and creates a stub
# authorized_keys.
set_up_ssh() {
  PATH_DIR_FOR_TEST="$(mktemp -d)"
  cp "${BT_TEMP_DIR}/ssh" "${PATH_DIR_FOR_TEST}"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # This authorized_keys file must not be empty, but its contents aren't used.
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata"
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"
}

# Sets up a device-finder mock. The implemented mock aims to produce minimal
# output that parses correctly but is otherwise uninteresting.
set_up_device_finder() {
  cat >"${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/device-finder.mock_side_effects" <<"SETVAR"
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

# Helpers.

# Runs a bash script. The function provides these conveniences over calling the
# script directly:
#
# * Rather than calling the bash script directly, this command explicitly
#   invokes Bash and propagates some option flags.
# * Rather than showing the bash output, this command only outputs output if a
#   test fails.
#
# Args: the script to run and all args to pass.
run_bash_script() {
  local shell_flags
  # propagate certain bash flags if present
  shell_flags=()
  local status
  if [[ $- == *x* ]]; then
    shell_flags+=(-x)
    # Run command with inherited stdout/stderr.
    bash "${shell_flags[@]}" "$@"
    status=$?
  else
    # Run command, capturing stdout/stderr. Only output those if the test fails.
    local output
    output="$(bash "${shell_flags[@]}" "$@" 2>&1)"
    status=$?
    if [[ $status != 0 ]]; then
      echo "${output}"
    fi
  fi
  return $status
}

# Verifies that the given arguments appear in the command line invocation of the
# most previously sourced mock state. Any arguments passed to this function will
# be searched for in the actual arguments. This succeeds if the arguments are
# found in adjacent positions in the correct order.
#
# This function only checks for presence. As a result, it will NOT verify any of
# the following:
#
# * The arguments only appear once.
# * The arguments don't appear with conflicting arguments.
# * Any given argument --foo isn't overridden, say with a --no-foo flag later.
#
# Args: any number of arguments to check.
# Returns: 0 if found; 1 if not found.
check_mock_has_args() {
  local expected=("$@")
  for j in "${!BT_MOCK_ARGS[@]}"; do
    local window=("${BT_MOCK_ARGS[@]:$j:${#expected}}")
    local found=true
    for k in "${!expected[@]}"; do
      if [[ "${expected[$k]}" != "${window[$k]}" ]]; then
        found=false
        break
      fi
    done
    if [[ "${found}" == "true" ]]; then
      return 0
    fi
  done
  BT_EXPECT false "Could not find ${expected[*]} in ${BT_MOCK_ARGS[*]}"
  return 1
}

# Verify that pm serve was run correctly.
TEST_fserve_starts_package_server() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # Run command.
  BT_EXPECT run_bash_script "${FSERVE_CMD}" --image generic-x64

  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/pm.mock_state"

  BT_EXPECT_EQ 6 ${#BT_MOCK_ARGS[@]}
  check_mock_has_args serve
  check_mock_has_args -repo "${FUCHSIA_WORK_DIR}/packages/amber-files"
  check_mock_has_args -l :8083

  # Verify that pm was only run once.
  BT_EXPECT_FILE_DOES_NOT_EXIST "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/pm.mock_state.1"
}

# Verifies that the package server has been correctly registered with the
# Fuchsia device.
TEST_fserve_registers_package_server() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # Run command.
  BT_EXPECT run_bash_script "${FSERVE_CMD}" --image generic-x64

  # shellcheck disable=SC1090
  source "${PATH_DIR_FOR_TEST}/ssh.mock_state"

  BT_EXPECT_EQ 8 "${#BT_MOCK_ARGS[@]}"
  check_mock_has_args -F "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/sshconfig"
  check_mock_has_args fe80::c0ff:eec0:ffee%coffee
  check_mock_has_args amber_ctl add_src -f "http://[fe80::c0ff:eec0:ffee]:8083/config.json"

  # Verify that ssh was only run once.
  BT_EXPECT_FILE_DOES_NOT_EXIST "${PATH_DIR_FOR_TEST}/ssh.mock_state.1"
}

# Verify image names are listed if the image is not found.
TEST_fpave_lists_images() {
  set_up_gsutil_multibucket

  BT_EXPECT_FAIL run_bash_script -x "${FSERVE_CMD}" --image unknown > list_images_output.txt  2>&1
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "image1 image2 image3"

  BT_EXPECT_FAIL run_bash_script "${FSERVE_CMD}" --bucket other --image unknown > list_images_output.txt 2>&1
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "list_images_output.txt" "image4 image5 image6 image1 image2 image3"
}

# Test initialization.
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fserve.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/bin/gsutil
  scripts/sdk/gn/base/tools/device-finder
  scripts/sdk/gn/base/tools/pm
  ssh
)

BT_SET_UP() {
  FUCHSIA_WORK_DIR="${BT_TEMP_DIR}/scripts/sdk/gn/base/images"
}

BT_INIT_TEMP_DIR() {
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta"

  # Create a stub SDK manifest.
  cp "${BT_DEPS_ROOT}/scripts/sdk/gn/testdata/meta/manifest.json" \
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta/manifest.json"
}

BT_RUN_TESTS "$@"
