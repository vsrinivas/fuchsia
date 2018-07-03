# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/astro.c \
    $(LOCAL_DIR)/astro-bluetooth.c \
    $(LOCAL_DIR)/astro-gpio.c \
    $(LOCAL_DIR)/astro-i2c.c \
    $(LOCAL_DIR)/astro-usb.c \
    $(LOCAL_DIR)/astro-display.c \
    $(LOCAL_DIR)/astro-touch.c \
    $(LOCAL_DIR)/astro-rawnand.c \
    $(LOCAL_DIR)/astro-sdio.c \
    $(LOCAL_DIR)/astro-canvas.c \

MODULE_STATIC_LIBS := \
    system/dev/lib/amlogic \
    system/ulib/ddk \
    system/ulib/sync

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk
