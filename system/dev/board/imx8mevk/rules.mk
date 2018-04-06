# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/imx8mevk.c \
    $(LOCAL_DIR)/imx8mevk-gpio.c \
    $(LOCAL_DIR)/imx8mevk-usb.c \


MODULE_STATIC_LIBS := \
    system/dev/soc/imx8m \
    system/ulib/ddk \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon

MODULE_HEADER_DEPS := \
    system/dev/soc/imx8m

include make/module.mk
