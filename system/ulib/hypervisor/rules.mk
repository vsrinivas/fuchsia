# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SO_NAME := hypervisor

MODULE_TYPE := userlib

MODULE_CFLAGS += \
    -Ithird_party/lib/acpica/source/include

MODULE_SRCS += \
    $(LOCAL_DIR)/acpi.c \
    $(LOCAL_DIR)/block.c \
    $(LOCAL_DIR)/guest.c \
    $(LOCAL_DIR)/io_apic.c \
    $(LOCAL_DIR)/io_port.c \
    $(LOCAL_DIR)/pci.c \
    $(LOCAL_DIR)/uart.c \
    $(LOCAL_DIR)/vcpu.c \
    $(LOCAL_DIR)/virtio.c \

ifeq ($(SUBARCH),x86-64)
MODULE_SRCS += \
    $(LOCAL_DIR)/decode.c
endif

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/virtio \
    third_party/ulib/acpica \

include make/module.mk
