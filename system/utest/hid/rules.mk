# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/hid-test.cpp \
    system/dev/input/hid/hid-parser.c \
    system/utest/hid-parser/hid-report-data.cpp

MODULE_NAME := hid-test

MODULE_FIDL_LIBS := system/fidl/zircon-input

MODULE_STATIC_LIBS := \
    system/ulib/hid-parser \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zxcpp

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/c

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-hidbus

include make/module.mk
