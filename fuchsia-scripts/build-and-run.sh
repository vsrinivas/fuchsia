#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

MODULAR_DIR="$( cd "$( dirname "$( dirname "${BASH_SOURCE[0]}" )" )" && pwd )"
ROOT_DIR="$( dirname "$(dirname $MODULAR_DIR)" )"

# Generate ninja files with modular autorun in bootfs. Modular autorun will
# launch story-manager.
$ROOT_DIR/packages/gn/gen.py -m modular-autorun

# Build Fuchsia and all its dependencies (including modular).
$ROOT_DIR/buildtools/ninja -C $ROOT_DIR/out/debug-x86-64

# Build Magenta.
(cd $ROOT_DIR/magenta && make -j32 magenta-pc-x86-64)

# Run Fuchsia on QEMU.
$ROOT_DIR/magenta/scripts/run-magenta-x86-64 -x \
  $ROOT_DIR/out/debug-x86-64/user.bootfs
