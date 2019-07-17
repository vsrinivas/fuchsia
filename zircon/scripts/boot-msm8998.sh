#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

OUT_DEFAULT="${DIR}/../../out/default"
KERNEL="${OUT_DEFAULT}/zedboot.zbi.gz.dtb"
RAMDISK="${OUT_DEFAULT}/dummy-ramdisk"

echo "foo" > "${RAMDISK}"
fastboot boot -n 4096 "${KERNEL}" "${RAMDISK}"
