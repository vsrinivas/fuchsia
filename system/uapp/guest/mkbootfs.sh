#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

BUILDDIR="${1:-build-magenta-pc-x86-64}"
KERNEL="${2:-$BUILDDIR/magenta.bin}"

echo "data/kernel.bin=$KERNEL" > /tmp/guest.manifest
$BUILDDIR/tools/mkbootfs \
    --target=boot \
    -o $BUILDDIR/bootdata-with-kernel.bin \
    $BUILDDIR/bootdata.bin \
    /tmp/guest.manifest
