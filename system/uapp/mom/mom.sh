#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Use this to start magenta with a handy magenta.bin sitting in the file system.
# This makes running mom a trivial case of... "mom /boot/data/magenta.bin"

MAG_X86="${MAG_X86:-build-magenta-pc-x86-64}"

echo "data/magenta.bin=$MAG_X86/magenta.bin" > /tmp/mom.manifest
$MAG_X86/tools/mkbootfs --target=boot -o $MAG_X86/bootdata-with-magenta.bin \
                        $MAG_X86/bootdata.bin /tmp/mom.manifest

killall bootserver
$MAG_X86/tools/bootserver $MAG_X86/magenta.bin $MAG_X86/bootdata-with-magenta.bin
