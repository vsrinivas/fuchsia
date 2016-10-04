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
	$(LOCAL_DIR)/pcie.cpp \
	$(LOCAL_DIR)/pcie_caps.cpp \
	$(LOCAL_DIR)/pcie_irqs.cpp \
	$(LOCAL_DIR)/pcie_topology.cpp

MODULE_DEPS += \
    lib/mxtl

include make/module.mk
