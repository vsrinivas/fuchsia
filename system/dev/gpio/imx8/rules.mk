# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).m

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/imx8-gpio.c \
    $(LOCAL_DIR)/imx8m-gpio.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon \

MODULE_HEADER_DEPS := system/dev/lib/imx8m

include make/module.mk

MODULE := $(LOCAL_DIR).m-mini

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/imx8-gpio.c \
    $(LOCAL_DIR)/imx8m-mini-gpio.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon \

MODULE_HEADER_DEPS := system/dev/lib/imx8m

include make/module.mk
