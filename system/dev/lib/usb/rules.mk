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

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/ddktl system/ulib/fbl
MODULE_LIBS := system/ulib/unittest \
    system/ulib/fdio \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \
    system/dev/lib/fake-bti \


MODULE_COMPILEFLAGS += \
    -Isystem/ulib/fit/include \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb \
    system/banjo/ddk-protocol-usb-composite \
    system/banjo/ddk-protocol-usb-request \

MODULE_PACKAGE := src

include make/module.mk
