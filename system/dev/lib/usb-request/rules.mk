# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib

MODULE_SRCS := $(LOCAL_DIR)/usb-request.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/c

include make/module.mk

#
# test
#
MODULE := $(LOCAL_DIR).usb-request-test

MODULE_NAME := usb-request-test

MODULE_TYPE := drivertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := \
  $(TEST_DIR)/ddk-test.c \
  $(TEST_DIR)/ddk-test-binding.c \
  $(TEST_DIR)/usb-request-test.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync system/dev/lib/usb-request

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/fdio \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

include make/module.mk
