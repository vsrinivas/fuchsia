# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := hypervisor-test

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/$(ARCH).S \
    $(LOCAL_DIR)/guest.cpp \
    $(LOCAL_DIR)/main.cpp \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async.loop-cpp \
    system/ulib/async.loop \
    system/ulib/fbl \
    system/ulib/hypervisor \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_CPPFLAGS := \
    -Isystem/ulib/hypervisor/arch/$(ARCH)/include \

ifeq ($(ARCH),x86)
MODULE_SRCS += \
    $(LOCAL_DIR)/decode.cpp \

MODULE_STATIC_LIBS += \
    system/ulib/pretty
endif

include make/module.mk
