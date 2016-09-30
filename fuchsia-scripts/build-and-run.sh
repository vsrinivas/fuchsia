#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

MODULAR_DIR="$( cd "$( dirname "$( dirname "${BASH_SOURCE[0]}" )" )" && pwd )"
ROOT_DIR="$( dirname "$(dirname $MODULAR_DIR)" )"
GOMA_FLAG=
JOB_COUNT_FLAG=

if [[ $1 == "--goma" ]]; then
  GOMA_FLAG=--goma
  JOB_COUNT_FLAG=-j1000
fi

# Build sysroot.
$ROOT_DIR/scripts/build-sysroot.sh -c -t x86_64

# Generate ninja files.
$ROOT_DIR/packages/gn/gen.py $GOMA_FLAG

# Build Fuchsia and all its dependencies, including modular.
$ROOT_DIR/buildtools/ninja -C $ROOT_DIR/out/debug-x86-64 $JOB_COUNT_FLAG

# Build Magenta.
(cd $ROOT_DIR/magenta && make -j32 magenta-pc-x86-64)

# Run Fuchsia on QEMU. To run modular now, type mojo:device_runner at
# the shell prompt.
$ROOT_DIR/magenta/scripts/run-magenta-x86-64 -x \
  $ROOT_DIR/out/debug-x86-64/user.bootfs

