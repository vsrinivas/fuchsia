# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

PLATFORM_BOARD_NAME := qemu

include kernel/target/arm64/boot-shim/rules.mk

# qemu needs a trampoline shim.
QEMU_BOOT_SHIM := $(BUILDDIR)/qemu-boot-shim.bin
EXTRA_BUILDDEPS += $(QEMU_BOOT_SHIM)
