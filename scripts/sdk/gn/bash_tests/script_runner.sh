#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Runs test scripts using the bash_test_framework.
#
set -e
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
TEST_DIR="$PWD"

# Find the root of the project
get_jiri_root() {
  local root_dir=""
  root_dir="${SCRIPT_SRC_DIR}"
  while [[ ! -d "${root_dir}/.jiri_root" ]]; do
    root_dir="$(dirname "${root_dir}")"
    if [[ ${#root_dir} -eq 1 ]]; then
      echo >&2 "Error! could not find the root of the project starting from $SCRIPT_SRC_DIR. The current working directory needs to be under the root of the project"
      find "${TEST_DIR}"
      exit 1
    fi
  done
  echo "${root_dir}"
}

launch_script() {
  local test_script_name=""
  test_script_name="$1"
  shift
  local test_script_path="${SCRIPT_SRC_DIR}/${test_script_name}"
  local bt_deps_root="${TEST_DIR}";

  # Check the local directory as the root of the test framework first,
  # then fall back to jiri_root.
  local test_framework_path="${bt_deps_root}/tools/devshell/tests/lib/bash_test_framework.sh"
  if [[ ! -e "${test_framework_path}" ]]; then
    echo "Could not find $test_framework_path"
    bt_deps_root="$(get_jiri_root)"
    test_framework_path="${bt_deps_root}/tools/devshell/tests/lib/bash_test_framework.sh"
  fi

  if [[ ! -f "${test_script_path}" ]]; then
    echo >&2 "Test script '${test_script_path}' not found. Aborting."
    return 1
  fi
  # propagate certain bash flags if present
  local shell_flags=()
  if [[ $- == *x* ]]; then
    shell_flags+=( -x )
  fi

  # Start a clean environment, load the bash_test_framework.sh,
  # then start the test script.
  # No quotes around EOF so variables are expanded when heredoc is processed.
  local -r launch_script="$(cat << EOF
  #set -x
export BT_DEPS_ROOT="${bt_deps_root}"
cd "\$BT_DEPS_ROOT"
source "${test_framework_path}" || exit \$?
source "${test_script_path}" || exit \$?
EOF
)"

echo "Launching test script $test_script_path"

  /usr/bin/env -i \
      USER="${USER}" \
      HOME="${HOME}" \
      bash "${shell_flags[@]}" \
      -c "${launch_script}" "${test_script_path}" "$@"
}

launch_script "$@"
