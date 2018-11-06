# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/bind.c \
    $(LOCAL_DIR)/mt8167.cpp \
    $(LOCAL_DIR)/mt8167-emmc.cpp \
    $(LOCAL_DIR)/mt8167-soc.cpp \
    $(LOCAL_DIR)/mt8167-gpio.cpp \
    $(LOCAL_DIR)/mt8167-display.cpp \
    $(LOCAL_DIR)/mt8167-i2c.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/mt8167 \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-gpio

include make/module.mk
