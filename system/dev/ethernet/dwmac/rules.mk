# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/dwmac.cpp \
    $(LOCAL_DIR)/dwmac-debug.cpp \
    $(LOCAL_DIR)/pinned-buffer.cpp


MODULE_HEADER_DEPS := \
    system/dev/lib/amlogic

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/pretty \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-ethernet-mac \
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-i2c \
    system/banjo/ddk-protocol-platform-device \
    system/banjo/ddk-protocol-test \


include make/module.mk

