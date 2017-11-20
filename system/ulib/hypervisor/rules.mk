# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SO_NAME := hypervisor

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/balloon.cpp \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/gpu.cpp \
    $(LOCAL_DIR)/guest.cpp \
    $(LOCAL_DIR)/input.cpp \
    $(LOCAL_DIR)/pci.cpp \
    $(LOCAL_DIR)/phys_mem.cpp \
    $(LOCAL_DIR)/uart.cpp \
    $(LOCAL_DIR)/vcpu.cpp \
    $(LOCAL_DIR)/virtio.cpp \
    $(LOCAL_DIR)/virtio_pci.cpp \

MODULE_HEADER_DEPS := \
    system/ulib/ddk \
    system/ulib/virtio \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/hid \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    system/ulib/block-client \
    system/ulib/fbl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_CPPFLAGS += \
    -Isystem/ulib/hypervisor/arch/$(ARCH)/include \

include system/ulib/hypervisor/arch/$(ARCH)/rules.mk
include make/module.mk
