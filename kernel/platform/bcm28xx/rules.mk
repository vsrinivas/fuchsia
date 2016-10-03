# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

WITH_SMP := 1
LK_HEAP_IMPLEMENTATION ?= cmpctmalloc

MODULE_SRCS += \
	$(LOCAL_DIR)/gpio.c \
	$(LOCAL_DIR)/intc.c \
	$(LOCAL_DIR)/platform.c \
	$(LOCAL_DIR)/miniuart.c \
	$(LOCAL_DIR)/atag.c

MEMBASE := 0x00000000

GLOBAL_DEFINES += \
	ARM_ARCH_WAIT_FOR_SECONDARIES=1

LINKER_SCRIPT += \
	$(BUILDDIR)/system-onesegment.ld

ARCH := arm64
ARM_CPU := cortex-a53

KERNEL_LOAD_OFFSET := 0x00080000
MEMSIZE ?= 0x20000000 # Limit to 512MB for now (GPU uses 1/2 of the 1GB total)

GLOBAL_DEFINES += \
	MEMBASE=$(MEMBASE) \
	MEMSIZE=$(MEMSIZE) \
	MMU_WITH_TRAMPOLINE=1 \
	BCM2837=1 \
	WITH_LIB_DEBUGLOG=1 \
	PLATFORM_SUPPORTS_PANIC_SHELL=1 \

MODULE_DEPS += \
	lib/cbuf \
	lib/fdt \
	dev/timer/arm_generic \
	dev/interrupt \

include make/module.mk

