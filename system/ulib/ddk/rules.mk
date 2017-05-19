# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/common/hid-fifo.c \
    $(LOCAL_DIR)/common/usb.c \
    $(LOCAL_DIR)/protocol/input.c \
    $(LOCAL_DIR)/protocol/platform-device.c \
    $(LOCAL_DIR)/io-buffer.c \
    $(LOCAL_DIR)/iotxn.c \

MODULE_STATIC_LIBS := system/ulib/pretty system/ulib/sync

MODULE_EXPORT := a

MDI_INCLUDES += system/ulib/ddk/include/mdi/ddk-defs.mdi

include make/module.mk


#
# iotxn-test - iotxn tests
#
MODULE := $(LOCAL_DIR).iotxn-test

MODULE_NAME := iotxn-test

MODULE_TYPE := drivertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := $(TEST_DIR)/iotxn-test.c

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/mxio \
    system/ulib/driver \
    system/ulib/magenta \
    system/ulib/c

include make/module.mk

#
# ddktl-test
#

MODULE := $(LOCAL_DIR).ddktl-test

MODULE_NAME := ddktl-test

MODULE_TYPE := drivertest

TEST_DIR := $(LOCAL_DIR)/test/ddktl

MODULE_SRCS := \
    $(TEST_DIR)/ddktl-test.cpp \
    $(TEST_DIR)/ddktl-test-binding.c \
    $(TEST_DIR)/device-tests.cpp \
    $(TEST_DIR)/ethernet-tests.cpp \
    $(TEST_DIR)/wlan-tests.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/mx \
    system/ulib/mxalloc \
    system/ulib/mxcpp \
    system/ulib/mxtl \

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/mxio \
    system/ulib/driver \
    system/ulib/magenta \
    system/ulib/c \

include make/module.mk
