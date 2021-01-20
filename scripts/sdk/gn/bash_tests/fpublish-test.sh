#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests for fpublish.

set -e

# Constants.
readonly PACKAGE_NAME=placeholder.far


# Verifies that the correct pm serve command is run by fpublish.
TEST_fpublish() {
  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fpublish.sh" "${PACKAGE_NAME}"

  # Verify that pm serve was run correctly.
  # shellcheck disable=SC1090
  source "${MOCKED_PM}.mock_state"

  # Expected commands to be run by fpublish.sh.
  local EXPECTED_PM_PUBLISH_ARGS=(
    _ANY_
    publish
    -a
    -r "${FUCHSIA_WORK_DIR}/packages/amber-files"
    -f "${PACKAGE_NAME}"
  )

  gn-test-check-mock-args "${EXPECTED_PM_PUBLISH_ARGS[@]}"


  # Verify that pm was only run once.
  BT_EXPECT_FILE_DOES_NOT_EXIST "${MOCKED_PM}.mock_state.1"
}

# Test initialization.
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fpublish.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/tools/x64/fconfig
  scripts/sdk/gn/base/tools/arm64/fconfig
  scripts/sdk/gn/base/tools/x64/pm
  scripts/sdk/gn/base/tools/arm64/pm
)

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"
  FUCHSIA_WORK_DIR="${HOME}/.fuchsia"

  MOCKED_PM="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/pm"

}

BT_RUN_TESTS "$@"
