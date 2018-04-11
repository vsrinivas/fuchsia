# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/arm_gicv2.c \
	$(LOCAL_DIR)/arm_gicv2m.c \
	$(LOCAL_DIR)/arm_gicv2m_msi.c \
	$(LOCAL_DIR)/arm_gicv2m_pcie.cpp \

MODULE_DEPS += \
	kernel/dev/interrupt \
	kernel/dev/pdev \
	kernel/dev/pdev/interrupt \
	kernel/lib/pow2_range_allocator

include make/module.mk
