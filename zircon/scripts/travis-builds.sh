#!/bin/sh

# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# restore config.mk from the cached configs directory
# if it exists
cp -f ./prebuilt/configs/config.mk ./prebuilt/config.mk

echo "Downloading Toolchain"
./scripts/download-prebuilt

# save config.mk to the configs directory so it will be
# cached along with the downloaded toolchain
cp -f ./prebuilt/config.mk ./prebuilt/configs

echo "Starting build '$PROJECT'"
make $PROJECT -j16
