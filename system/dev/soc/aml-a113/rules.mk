# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/a113-bus.c \
    $(LOCAL_DIR)/a113-gpio.c \
    $(LOCAL_DIR)/a113-i2c.c \
    $(LOCAL_DIR)/aml-i2c.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon \

include make/module.mk
