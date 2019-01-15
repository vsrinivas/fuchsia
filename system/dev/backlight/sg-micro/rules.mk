# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/sgm37603a.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-hardware-backlight \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-clk \
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-i2c \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk
