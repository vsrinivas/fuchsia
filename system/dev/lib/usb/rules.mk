# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib

MODULE_SRCS := \
	$(LOCAL_DIR)/usb.c \
	$(LOCAL_DIR)/usb-request.c \
    $(LOCAL_DIR)/usb-wrapper.cpp \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/ddktl system/ulib/fbl
MODULE_LIBS := system/ulib/c system/ulib/unittest

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb \
    system/banjo/ddk-protocol-usb-composite \
    system/banjo/ddk-protocol-usb-request \

MODULE_PACKAGE := src

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_NAME := usb-wrapper-test

MODULE_SRCS := \
    $(LOCAL_DIR)/tests/usb-wrapper-tests.cpp \
    $(LOCAL_DIR)/tests/main.c \
    $(LOCAL_DIR)/usb.c \
    $(LOCAL_DIR)/usb-wrapper.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/fake-bti \
	system/ulib/ddk \
	system/ulib/ddktl \
	system/ulib/fbl \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/driver \
    system/ulib/zircon \
	system/ulib/unittest \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb \
    system/banjo/ddk-protocol-usb-composite \
    system/banjo/ddk-protocol-usb-request \

MODULE_PACKAGE := src

include make/module.mk

TEST_DIR := $(LOCAL_DIR)/test

MODULE := $(LOCAL_DIR).test2

MODULE_TYPE := usertest

MODULE_NAME := usb-unittest

MODULE_SRCS += \
    $(LOCAL_DIR)/usb-request.c \
    $(TEST_DIR)/main.cpp \
    $(TEST_DIR)/usb-request-pool-test.cpp \
    $(TEST_DIR)/usb-request-queue-test.cpp \
    $(TEST_DIR)/usb-request-test.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/fake-bti \
    system/dev/lib/fake_ddk \
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
