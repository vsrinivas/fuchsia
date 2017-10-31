# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SO_NAME := hypervisor

MODULE_TYPE := userlib

MODULE_CPPFLAGS += \
    -Ithird_party/lib/acpica/source/include

MODULE_SRCS += \
    $(LOCAL_DIR)/acpi.cpp \
    $(LOCAL_DIR)/balloon.cpp \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/gpu.cpp \
    $(LOCAL_DIR)/guest.cpp \
    $(LOCAL_DIR)/input.cpp \
    $(LOCAL_DIR)/io_apic.cpp \
    $(LOCAL_DIR)/local_apic.cpp \
    $(LOCAL_DIR)/pci.cpp \
    $(LOCAL_DIR)/phys_mem.cpp \
    $(LOCAL_DIR)/uart.cpp \
    $(LOCAL_DIR)/vcpu.cpp \
    $(LOCAL_DIR)/virtio.cpp \
    $(LOCAL_DIR)/virtio_pci.cpp \

ifeq ($(SUBARCH),x86-64)
MODULE_SRCS += \
    $(LOCAL_DIR)/decode.cpp \
    $(LOCAL_DIR)/io_port.cpp
endif

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    system/ulib/block-client \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/fdio \
    system/ulib/hid \
    system/ulib/sync \
    system/ulib/virtio \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/acpica \

include make/module.mk
