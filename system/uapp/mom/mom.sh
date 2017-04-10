#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Use this to start magenta with a handy magenta.bin sitting in the file system.
# This makes running mom a trivial case of... "mom /boot/data/magenta.bin"

BUILDDIR="${BUILDDIR:-build-magenta-pc-x86-64}"

echo "data/magenta.bin=$BUILDDIR/magenta.bin" > /tmp/mom.manifest
$BUILDDIR/tools/mkbootfs \
    --target=boot \
    -o $BUILDDIR/bootdata-with-magenta.bin \
    $BUILDDIR/bootdata.bin \
    /tmp/mom.manifest

exec $BUILDDIR/tools/bootserver \
    $BUILDDIR/magenta.bin \
    $BUILDDIR/bootdata-with-magenta.bin
