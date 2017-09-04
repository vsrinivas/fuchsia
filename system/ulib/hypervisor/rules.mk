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
    $(LOCAL_DIR)/guest.cpp \
    $(LOCAL_DIR)/io_apic.cpp \
    $(LOCAL_DIR)/io_port.cpp \
    $(LOCAL_DIR)/pci.cpp \
    $(LOCAL_DIR)/uart.cpp \
    $(LOCAL_DIR)/vcpu.cpp \
    $(LOCAL_DIR)/virtio.cpp \
    $(LOCAL_DIR)/virtio_pci.cpp \
    $(LOCAL_DIR)/virtio_pci_legacy.cpp \

ifeq ($(SUBARCH),x86-64)
MODULE_SRCS += \
    $(LOCAL_DIR)/decode.cpp
endif

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/virtio \
    third_party/ulib/acpica \
    system/ulib/mxcpp \
    system/ulib/fbl \

include make/module.mk
