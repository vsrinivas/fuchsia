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

netcp ${FUCHSIA_BUILD_DIR}/component_context_test :/system/apps/modular_tests/component_context_test
netcp ${FUCHSIA_BUILD_DIR}/component_context_test_agent1 :/system/apps/modular_tests/component_context_test_agent1
netcp ${FUCHSIA_BUILD_DIR}/component_context_test_agent2 :/system/apps/modular_tests/component_context_test_agent2
netcp ${FUCHSIA_BUILD_DIR}/component_context_unstoppable_agent :/system/apps/modular_tests/component_context_unstoppable_agent

${FUCHSIA_DIR}/apps/modular/src/test_runner/run_test "device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=/system/apps/modular_tests/component_context_test"
