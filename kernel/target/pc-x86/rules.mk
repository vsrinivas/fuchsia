# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

PLATFORM := pc

MODULE_SRCS := $(LOCAL_DIR)/empty.cpp

MODULE_DEPS := \
    kernel/dev/intel_rng

include make/module.mk

# build kernel-bootdata for fuchisa build
KERNEL_BOOTDATA := $(BUILDDIR)/x86-kernel-bootdata.bin
$(KERNEL_BOOTDATA): $(MKBOOTFS)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)$(MKBOOTFS) -o $@ --empty

kernel-only: $(KERNEL_BOOTDATA)
kernel: $(KERNEL_BOOTDATA)

# generate board list for the fuchsia build
ZIRCON_BOARD_LIST := $(BUILDDIR)/export/boards.list
BOARDS := "x86"

$(ZIRCON_BOARD_LIST):
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)echo $(BOARDS) > $@

GENERATED += $(ZIRCON_BOARD_LIST)
EXTRA_BUILDDEPS += $(ZIRCON_BOARD_LIST)

packages: $(ZIRCON_BOARD_LIST)
