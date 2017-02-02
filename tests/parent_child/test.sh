#!/bin/sh

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Raw set of commands used to invoke the parent_child test.
# Eventually it would be nice to make tests configuration driven.
if [ "${FUCHSIA_BUILD_DIR}" = "" ]; then
  echo "Please source //scripts/env.sh and run fset."
  exit -1;
fi

netcp ${FUCHSIA_BUILD_DIR}/parent_module :/tmp/tests/parent_child/parent_module
netcp ${FUCHSIA_BUILD_DIR}/child_module :/tmp/tests/parent_child/child_module

${FUCHSIA_DIR}/apps/modular/test_runner/tools/run_test "/system/apps/bootstrap /system/apps/device_runner --user_shell=file:///system/apps/dev_user_shell --user_shell_args=--root_module=/tmp/tests/parent_child/parent_module"
