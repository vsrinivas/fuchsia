#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

function HELP {
  echo "help:"
  echo "--goma                    : Use Goma"
  echo "--sysroot                 : Build sysroot"
  echo "--help"
  exit 1
}

MODULAR_DIR="$( cd "$( dirname "$( dirname "${BASH_SOURCE[0]}" )" )" && pwd )"
ROOT_DIR="$( dirname "$(dirname $MODULAR_DIR)" )"

GOMA=0
SYSROOT=0

for i in "$@"; do
  case $i in
    --goma)
      GOMA=1
      shift
      ;;
    --sysroot)
      SYSROOT=1
      shift
      ;;
    --help)
      shift
      HELP
      ;;
    *)
      echo unrecognized option
      HELP
      ;;
  esac
done

if [ "$GOMA"  -eq 1 ]; then
  GOMA_FLAG=--goma
  case $OSTYPE in
    darwin*) JOB_COUNT_FLAG=-j50 ;;
    linux-gnu) JOB_COUNT_FLAG=-j1000 ;;
    *) JOB_COUNT_FLAG= ;;
  esac
else
  GOMA_FLAG=
  JOB_COUNT_FLAG=
fi

if [ "$SYSROOT" -eq 1 ]; then
  # Build sysroot.
  $ROOT_DIR/scripts/build-sysroot.sh -c -t x86_64
fi

# Generate ninja files.
$ROOT_DIR/packages/gn/gen.py $GOMA_FLAG

# Build Fuchsia and all its dependencies, including modular.
$ROOT_DIR/buildtools/ninja -C $ROOT_DIR/out/debug-x86-64 $JOB_COUNT_FLAG

# Build Magenta.
(cd $ROOT_DIR/magenta && make -j32 magenta-pc-x86-64)

# Run Fuchsia on QEMU. To run modular now, type mojo:device_runner at
# the shell prompt.
$ROOT_DIR/magenta/scripts/run-magenta-x86-64 -x \
  $ROOT_DIR/out/debug-x86-64/user.bootfs -g -m 2048

