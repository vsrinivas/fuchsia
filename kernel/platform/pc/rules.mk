# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

CPU := generic

MODULE_DEPS += \
    lib/cbuf \

MODULE_SRCS += \
    $(LOCAL_DIR)/acpi.c \
    $(LOCAL_DIR)/console.c \
    $(LOCAL_DIR)/debug.c \
    $(LOCAL_DIR)/hpet.cpp \
    $(LOCAL_DIR)/interrupts.c \
    $(LOCAL_DIR)/keyboard.c \
    $(LOCAL_DIR)/memory.c \
    $(LOCAL_DIR)/pci.c \
    $(LOCAL_DIR)/pic.c \
    $(LOCAL_DIR)/platform.c \
    $(LOCAL_DIR)/power.c \
    $(LOCAL_DIR)/timer.c \
    $(LOCAL_DIR)/watchdog.c \

MODULE_DEPS += \
    lib/acpica \
    lib/gfxconsole \
    lib/fixed_point \
    lib/pow2_range_allocator \
    dev/interrupt \
    dev/pcie \

KERNEL_DEFINES += \
    PLATFORM_SUPPORTS_PANIC_SHELL=1

LK_HEAP_IMPLEMENTATION ?= cmpctmalloc

include make/module.mk

