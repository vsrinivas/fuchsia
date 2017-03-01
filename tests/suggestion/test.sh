#!/bin/sh

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Raw set of commands used to invoke the component_context test.
# Eventually it would be nice to make tests configuration driven.
if [ "${FUCHSIA_BUILD_DIR}" = "" ]; then
  echo "Please source //scripts/env.sh and run fset."
  exit -1;
fi

netcp ${FUCHSIA_BUILD_DIR}/suggestion_proposal_test_module :/system/apps/modular_tests/suggestion_proposal_test_module

netcp ${FUCHSIA_BUILD_DIR}/suggestion_test_user_shell :/system/apps/suggestion_test_user_shell

${FUCHSIA_DIR}/apps/modular/src/test_runner/run_test "device_runner --user_shell=suggestion_test_user_shell"
