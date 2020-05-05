#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is the header for a generated script for testing the bash scripts
# See the "run_all_bash_tests_driver" target.

set -eu

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
declare -r SCRIPT_SRC_DIR

script_runner() {
    "${SCRIPT_SRC_DIR}/script_runner.sh" "$@"
}

