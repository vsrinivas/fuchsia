# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/debug.cpp \
	$(LOCAL_DIR)/pcie_bridge.cpp \
	$(LOCAL_DIR)/pcie_bus_driver.cpp \
	$(LOCAL_DIR)/pcie_device.cpp \
	$(LOCAL_DIR)/pcie_caps.cpp \
	$(LOCAL_DIR)/pcie_irqs.cpp

MODULE_DEPS += \
    lib/mxtl \
    lib/region-alloc

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
# TODO(mcgrathr,johngro): This is only needed because of the unclean
# use of list_node in PcieDevice.irq_.legacy; when that is cleaned up
# then this should be removed.
MODULE_COMPILEFLAGS += -Wno-invalid-offsetof
endif

include make/module.mk
