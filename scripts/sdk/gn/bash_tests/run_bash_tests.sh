#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
declare -r SCRIPT_SRC_DIR

# Return failure if any test fails.
"${SCRIPT_SRC_DIR}/script_runner.sh" fuchsia-common-tests.sh
"${SCRIPT_SRC_DIR}/script_runner.sh" fpave-test.sh
"${SCRIPT_SRC_DIR}/script_runner.sh" fpublish-test.sh
"${SCRIPT_SRC_DIR}/script_runner.sh" fserve-test.sh
"${SCRIPT_SRC_DIR}/script_runner.sh" fssh-test.sh
"${SCRIPT_SRC_DIR}/script_runner.sh" femu-test.sh
"${SCRIPT_SRC_DIR}/script_runner.sh" fdevtools-test.sh
