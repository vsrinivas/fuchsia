# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Project file to build magenta + user space on top of qemu
# for 32bit arm (cortex-a15)

EMBED_USER_BOOTFS:=true

MODULES +=

include project/virtual/test.mk
include project/virtual/user.mk
include project/target/qemu-virt-a15.mk

