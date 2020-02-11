#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests for fpublish.

set -e

# Constants.
readonly PACKAGE_NAME=placeholder.far

# Paths.
SCRIPT_SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# Verifies that the correct pm serve command is run by fpublish.
TEST_fpublish() {
  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fpublish.sh" "${PACKAGE_NAME}"

  # Verify that pm serve was run correctly.
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/sdk/tools/pm.mock_state"
  local PM_ARGS=("${BT_MOCK_ARGS[@]:1}")

  # Expected commands to be run by fpublish.sh.
  local EXPECTED_PM_PUBLISH_ARGS=(
    publish
    -a
    -r "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/packages/amber-files"
    -f "${PACKAGE_NAME}"
  )

  BT_EXPECT_EQ ${#EXPECTED_PM_PUBLISH_ARGS[@]} ${#PM_ARGS[@]}
  for i in "${!EXPECTED_PM_PUBLISH_ARGS[@]}"; do
    if [[ "$i" == "0" ]]; then
      # The path to pm isn't relevant. The fact that the pm mock state is
      # available is sufficient verification that pm was called.
      continue
    fi
    BT_EXPECT_EQ "${EXPECTED_PM_PUBLISH_ARGS[$i]}" "${PM_ARGS[$i]}"
  done

  # Verify that pm was only run once.
  BT_EXPECT_FILE_DOES_NOT_EXIST "${BT_TEMP_DIR}/scripts/sdk/gn/base/sdk/tools/pm.mock_state.1"
}

# Test initialization.
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fpublish.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
)
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/sdk/tools/pm
)

BT_RUN_TESTS "$@"
