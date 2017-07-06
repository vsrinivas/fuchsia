# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/platform.cpp \
	$(LOCAL_DIR)/bcm28xx-spin.S \

LINKER_SCRIPT += \
	$(BUILDDIR)/system-onesegment.ld

ARCH := arm64
ARM_CPU := cortex-a53

MODULE_DEPS += \
	kernel/lib/cbuf \
	kernel/lib/mdi \
	kernel/lib/memory_limit \
	kernel/dev/bcm28xx \
	kernel/dev/pcie \
	kernel/dev/pdev \
	kernel/dev/timer/arm_generic \
	kernel/dev/interrupt/arm_gicv2 \
	kernel/dev/interrupt/arm_gicv3 \
	kernel/dev/interrupt/bcm28xx \
	kernel/dev/psci \
	kernel/dev/qemu \
	kernel/dev/uart/amlogic_s905 \
	kernel/dev/uart/bcm28xx \
	kernel/dev/uart/pl011 \

include make/module.mk
