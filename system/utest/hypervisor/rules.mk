# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := hypervisor-test

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/$(ARCH).S \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/gpu.cpp \
    $(LOCAL_DIR)/guest.cpp \
    $(LOCAL_DIR)/input.cpp \
    $(LOCAL_DIR)/pci.cpp \
    $(LOCAL_DIR)/virtio_queue.cpp \
    $(LOCAL_DIR)/virtio_queue_fake.cpp \

MODULE_HEADER_DEPS := \
    system/ulib/ddk \
    system/ulib/hid \
    system/ulib/virtio \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/hypervisor \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \

MODULE_CPPFLAGS := \
    -Isystem/ulib/hypervisor/arch/$(ARCH)/include \

ifeq ($(ARCH),x86)
MODULE_SRCS += \
    $(LOCAL_DIR)/decode.cpp \
    $(LOCAL_DIR)/page_table.cpp \

MODULE_STATIC_LIBS += \
    system/ulib/pretty
endif

include make/module.mk
