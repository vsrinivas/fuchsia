#!/bin/sh

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [ "$FUCHSIA_DIR" = "" ]; then
  echo "Please source //scripts/env.sh and run fset."
  exit -1;
fi

$FUCHSIA_DIR/apps/modular/tests/test.sh --test_name=suggestion "$@"
