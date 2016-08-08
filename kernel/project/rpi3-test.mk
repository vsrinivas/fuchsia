# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# main project for qemu-aarch64
MODULES +=


ENABLE_BUILD_LISTFILES:=true
EMBED_USER_BOOTFS:=true

include project/virtual/user.mk
include project/virtual/test.mk
include project/target/rpi3-a53.mk

