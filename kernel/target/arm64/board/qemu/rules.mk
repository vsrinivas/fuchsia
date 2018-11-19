# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

PLATFORM_BOARD_NAME := qemu

include kernel/target/arm64/boot-shim/rules.mk

# qemu needs a shimmed kernel
QEMU_ZIRCON := $(BUILDDIR)/qemu-zircon.bin
QEMU_BOOT_SHIM := $(BUILDDIR)/qemu-boot-shim.bin

# prepend shim to kernel image
$(QEMU_ZIRCON): $(QEMU_BOOT_SHIM) $(KERNEL_ZBI)
	$(call BUILDECHO,generating $@)
	$(NOECHO)cat $(QEMU_BOOT_SHIM) $(KERNEL_ZBI) > $@

GENERATED += $(QEMU_ZIRCON)
EXTRA_BUILDDEPS += $(QEMU_ZIRCON)
