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
	$(LOCAL_DIR)/pci_config.cpp \
	$(LOCAL_DIR)/pcie_bridge.cpp \
	$(LOCAL_DIR)/pcie_bus_driver.cpp \
	$(LOCAL_DIR)/pcie_caps.cpp \
	$(LOCAL_DIR)/pcie_device.cpp \
	$(LOCAL_DIR)/pcie_irqs.cpp \
	$(LOCAL_DIR)/pcie_root.cpp \
	$(LOCAL_DIR)/pcie_upstream_node.cpp

MODULE_DEPS += \
    kernel/lib/mxcpp \
    kernel/lib/mxtl \
    kernel/lib/region-alloc

MODULE_CPPFLAGS += -Wno-invalid-offsetof

include make/module.mk
