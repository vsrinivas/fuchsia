#!/bin/sh

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [ "$FUCHSIA_DIR" = "" ]; then
  echo "Please source //scripts/env.sh and run fset."
  exit -1;
fi

# By default, this script syncs the files listed in the modular_tests.json.
# To prevent this behavior, add --no-sync when calling this script, which will
# override the --sync parameter below.
$FUCHSIA_DIR/apps/test_runner/src/run_test \
  --test_file=$FUCHSIA_DIR/apps/modular/tests/modular_tests.json --sync "$@"
