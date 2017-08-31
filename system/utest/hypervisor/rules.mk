# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/guest.cpp \

ifeq ($(SUBARCH),x86-64)
MODULE_SRCS += \
    $(LOCAL_DIR)/decode.cpp \
    $(LOCAL_DIR)/page_table.cpp \
    $(LOCAL_DIR)/pci.cpp \
    $(LOCAL_DIR)/uart.cpp \
    $(LOCAL_DIR)/vcpu.cpp \
    $(LOCAL_DIR)/x86-64.S

MODULE_STATIC_LIBS := \
    system/ulib/pretty
endif

MODULE_NAME := hypervisor-test

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/hypervisor \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/unittest \

MODULE_STATIC_LIBS += \
    system/ulib/ddk \
    system/ulib/virtio \

include make/module.mk
