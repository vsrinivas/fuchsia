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

include make/board.mk

KDTBTOOL=$(BUILDDIR)/tools/mkkdtb

OUT_ZIRCON_ZIMAGE_KDTB := $(BUILDDIR)/z$(LKNAME).kdtb

$(OUT_ZIRCON_ZIMAGE_KDTB): $(OUT_ZIRCON_ZIMAGE) $(DEVICE_TREE) $(KDTBTOOL)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(KDTBTOOL) $(OUT_ZIRCON_ZIMAGE) $(DEVICE_TREE) $@

EXTRA_BUILDDEPS += $(OUT_ZIRCON_ZIMAGE_KDTB)
GENERATED += $(OUT_ZIRCON_ZIMAGE_KDTB)
