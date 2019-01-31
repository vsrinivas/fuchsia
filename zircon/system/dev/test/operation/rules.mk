# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_NAME := operation-test

MODULE_SRCS += \
    $(LOCAL_DIR)/operation-test.cpp \
    $(LOCAL_DIR)/operation-pool-test.cpp \
    $(LOCAL_DIR)/operation-queue-test.cpp \
    $(LOCAL_DIR)/main.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/fake_ddk \
    system/dev/lib/operation \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
