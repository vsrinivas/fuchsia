# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

CPU := generic

MODULE_SRCS += \
    $(LOCAL_DIR)/acpi.cpp \
    $(LOCAL_DIR)/console.cpp \
    $(LOCAL_DIR)/debug.cpp \
    $(LOCAL_DIR)/hpet.cpp \
    $(LOCAL_DIR)/interrupts.cpp \
    $(LOCAL_DIR)/keyboard.cpp \
    $(LOCAL_DIR)/memory.cpp \
    $(LOCAL_DIR)/pcie_quirks.cpp \
    $(LOCAL_DIR)/pic.cpp \
    $(LOCAL_DIR)/platform.cpp \
    $(LOCAL_DIR)/platform_pcie.cpp \
    $(LOCAL_DIR)/power.cpp \
    $(LOCAL_DIR)/timer.cpp \

MODULE_DEPS += \
    third_party/lib/acpica \
    third_party/lib/cksum \
    kernel/lib/cbuf \
    kernel/lib/gfxconsole \
    kernel/lib/fixed_point \
    kernel/lib/memory_limit \
    kernel/lib/fbl \
    kernel/lib/pow2_range_allocator \
    kernel/lib/version \
    kernel/dev/interrupt \
    kernel/dev/pcie \

KERNEL_DEFINES += \
    PLATFORM_SUPPORTS_PANIC_SHELL=1

SMP_MAX_CPUS ?= 8

include make/module.mk

