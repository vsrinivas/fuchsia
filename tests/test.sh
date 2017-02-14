#!/bin/sh

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [ "${FUCHSIA_BUILD_DIR}" = "" ]; then
  echo "Please source //scripts/env.sh and run fset."
  exit -1;
fi

# TODO(mesch): This is redundant with modular_tests.json, we should read this
# from there instead.
netcp ${FUCHSIA_BUILD_DIR}/parent_module :/tmp/tests/parent_module
netcp ${FUCHSIA_BUILD_DIR}/child_module :/tmp/tests/child_module
netcp ${FUCHSIA_BUILD_DIR}/component_context_test :/tmp/tests/component_context_test
netcp ${FUCHSIA_BUILD_DIR}/component_context_test_agent1 :/tmp/tests/component_context_test_agent1
netcp ${FUCHSIA_BUILD_DIR}/component_context_test_agent2 :/tmp/tests/component_context_test_agent2

${FUCHSIA_DIR}/apps/modular/src/test_runner/run_test --test_file=$FUCHSIA_DIR/apps/modular/tests/modular_tests.json
