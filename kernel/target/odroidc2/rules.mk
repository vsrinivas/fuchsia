# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := generic-arm

PLATFORM_VID := 3   # PDEV_VID_AMLOGIC
PLATFORM_PID := 1   # PDEV_PID_AMLOGIC_S905
PLATFORM_BOARD_NAME := odroid-c2

# build MDI
MDI_SRCS := $(LOCAL_DIR)/odroidc2.mdi

# prepend Linux header to kernel image

HEADER_TOOL := $(LOCAL_DIR)/prepend-header.py
KERNEL_LOAD_OFFSET := 0x10280000
KERNEL_IMAGE := $(BUILDDIR)/odroid-zircon.bin

$(KERNEL_IMAGE): $(HEADER_TOOL) $(OUTLKBIN)
	$(HEADER_TOOL) --kernel $(OUTLKBIN) --load_offset $(KERNEL_LOAD_OFFSET) --output $(KERNEL_IMAGE)

EXTRA_KERNELDEPS += $(KERNEL_IMAGE)
