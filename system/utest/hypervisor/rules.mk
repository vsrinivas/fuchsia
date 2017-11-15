# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/gpu.cpp \
    $(LOCAL_DIR)/guest.cpp \
    $(LOCAL_DIR)/input.cpp \
    $(LOCAL_DIR)/virtio_queue.cpp \
    $(LOCAL_DIR)/virtio_queue_fake.cpp \

ifeq ($(ARCH),arm64)
MODULE_SRCS += \
    $(LOCAL_DIR)/arm64.S
else ifeq ($(SUBARCH),x86-64)
MODULE_SRCS += \
    $(LOCAL_DIR)/decode.cpp \
    $(LOCAL_DIR)/page_table.cpp \
    $(LOCAL_DIR)/pci.cpp \
    $(LOCAL_DIR)/x86-64.S

MODULE_STATIC_LIBS := \
    system/ulib/pretty
endif

MODULE_NAME := hypervisor-test

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/hid \
    system/ulib/hypervisor \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_STATIC_LIBS += \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/virtio \
    system/ulib/zx \

include make/module.mk
