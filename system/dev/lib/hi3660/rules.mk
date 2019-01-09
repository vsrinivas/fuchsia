# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/hi3660.c \
    $(LOCAL_DIR)/hi3660-gpios.c \
    $(LOCAL_DIR)/hi3660-usb.c \
    $(LOCAL_DIR)/hi3660-i2c.c \
    $(LOCAL_DIR)/hi3660-dsi.c \
    $(LOCAL_DIR)/hi3660-ufs.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/dev/gpio/pl061 \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-gpioimpl \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk
