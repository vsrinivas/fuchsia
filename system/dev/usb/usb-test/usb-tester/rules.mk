# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/bind.c \
    $(LOCAL_DIR)/usb-tester.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/sync \
    system/dev/lib/usb \
    system/ulib/zxcpp \

MODULE_FIDL_LIBS := system/fidl/zircon-usb-tester

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

include make/module.mk
