# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_NAME := usb-unittest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/usb-request-pool-test.cpp \
    $(LOCAL_DIR)/usb-request-queue-test.cpp \
    $(LOCAL_DIR)/usb-request-test.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/fake_ddk \
    system/dev/lib/fake-bti \
    system/dev/lib/operation \
    system/dev/lib/usb \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb \
    system/banjo/ddk-protocol-usb-request \

include make/module.mk
