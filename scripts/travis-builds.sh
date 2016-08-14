#!/bin/sh

# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

echo "Downloading Toolchain"
./scripts/download-toolchain

echo "Starting build '$PROJECT'"
make $PROJECT -j4 ENABLE_BUILD_SYSROOT=true
