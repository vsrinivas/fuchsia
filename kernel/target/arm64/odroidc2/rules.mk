# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM_VID := 7   # PDEV_VID_HARDKERNEL
PLATFORM_PID := 1   # PDEV_PID_ODROID_C2
PLATFORM_BOARD_NAME := odroidc2
PLATFORM_MDI_SRCS := $(LOCAL_DIR)/odroidc2.mdi

include make/board.mk

# prepend Linux header to kernel image

HEADER_TOOL := $(LOCAL_DIR)/prepend-header.py
KERNEL_LOAD_OFFSET := 0x10280000
KERNEL_IMAGE := $(BUILDDIR)/odroidc2-zircon.bin

$(KERNEL_IMAGE): $(HEADER_TOOL) $(OUTLKBIN)
	$(NOECHO)$(HEADER_TOOL) --kernel $(OUTLKBIN) --load_offset $(KERNEL_LOAD_OFFSET) --output $(KERNEL_IMAGE)

EXTRA_KERNELDEPS += $(KERNEL_IMAGE)
