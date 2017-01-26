# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ARCH := arm64
ARM_CPU ?= cortex-a53
WITH_SMP ?= 1

LK_HEAP_IMPLEMENTATION ?= cmpctmalloc

MODULE_SRCS += \
    $(LOCAL_DIR)/debug.c \
    $(LOCAL_DIR)/platform.c \
    $(LOCAL_DIR)/platform_pcie.cpp \
    $(LOCAL_DIR)/secondary_boot.S \
    $(LOCAL_DIR)/uart.c

MEMBASE := 0x40000000
MEMSIZE ?= 0x08000000   # 512MB
KERNEL_LOAD_OFFSET := 0x10000 # 64k

MODULE_DEPS += \
    lib/cbuf \
    lib/fdt \
    dev/pcie \
    dev/timer/arm_generic \
    dev/interrupt/arm_gicv2m

KERNEL_DEFINES += \
    MEMBASE=$(MEMBASE) \
    MEMSIZE=$(MEMSIZE) \
    PLATFORM_SUPPORTS_PANIC_SHELL=1

LINKER_SCRIPT += \
    $(BUILDDIR)/system-onesegment.ld

include make/module.mk
