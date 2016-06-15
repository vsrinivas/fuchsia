# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# main project for qemu-arm32
ARCH := arm
ARM_CPU := cortex-a15

include project/target/qemu-virt.mk

