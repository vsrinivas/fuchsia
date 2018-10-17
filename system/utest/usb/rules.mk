# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += $(LOCAL_DIR)/usb-test.c

MODULE_NAME := usb-test

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/c \

MODULE_FIDL_LIBS := system/fidl/zircon-usb-tester

include make/module.mk
