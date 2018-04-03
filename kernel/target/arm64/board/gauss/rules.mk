# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

DEVICE_TREE := $(GET_LOCAL_DIR)/device-tree.dtb

PLATFORM_VID := 3   # PDEV_VID_GOOGLE
PLATFORM_PID := 1   # PDEV_PID_GAUSS
PLATFORM_BOARD_NAME := gauss
PLATFORM_MDI_SRCS := $(LOCAL_DIR)/gauss.mdi
PLATFORM_USE_SHIM := true

include make/board.mk

KDTBTOOL=$(BUILDDIR)/tools/mkkdtb

GAUSS_ZIRCON := $(BUILDDIR)/gauss-zircon.bin
GAUSS_ZZIRCON := $(BUILDDIR)/gauss-zzircon.bin
GAUSS_ZZIRCON_KDTB := $(BUILDDIR)/gauss-zzircon.kdtb

# prepend shim to kernel image
$(GAUSS_ZIRCON): $(MKBOOTFS) $(BOOT_SHIM_BIN) $(OUTLKBIN)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(MKBOOTFS) -o $@ $(OUTLKBIN) --header $(BOOT_SHIM_BIN) --header-align $(KERNEL_ALIGN)

$(GAUSS_ZZIRCON): $(GAUSS_ZIRCON)
	$(call BUILDECHO,gzipping image $@)
	$(NOECHO)gzip -c $< > $@

$(GAUSS_ZZIRCON_KDTB): $(GAUSS_ZZIRCON) $(DEVICE_TREE) $(KDTBTOOL)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(KDTBTOOL) $(GAUSS_ZZIRCON) $(DEVICE_TREE) $@

EXTRA_BUILDDEPS += $(GAUSS_ZZIRCON_KDTB)
GENERATED += $(GAUSS_ZZIRCON_KDTB)
