# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_PACKAGE := static

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/pdev.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-i2c \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk

#
# ddktl-test
#

MODULE := $(LOCAL_DIR).test

MODULE_NAME := ddktl-test

MODULE_TYPE := drivertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := \
    $(TEST_DIR)/ddktl-test.cpp \
    $(TEST_DIR)/ddktl-test-binding.c \
    $(TEST_DIR)/device-tests.cpp \
    $(TEST_DIR)/ethernet-tests.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/fdio \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-ethernet \
    system/banjo/ddk-protocol-test \

include make/module.mk
