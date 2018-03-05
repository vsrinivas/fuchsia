# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(ARCH),x86)

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += $(LOCAL_DIR)/main.cpp

MODULE_NAME := x86-umip-test

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    system/ulib/zx \

include make/module.mk

endif
