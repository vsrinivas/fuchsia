# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/astro-display.c \
    $(LOCAL_DIR)/osd.c \
    $(LOCAL_DIR)/backlight.c \
    $(LOCAL_DIR)/aml-dsi-host.c \
    $(LOCAL_DIR)/lcd.c \
    $(LOCAL_DIR)/display-debug.c \
    $(LOCAL_DIR)/display-clock.c \
    $(LOCAL_DIR)/dw-mipi-dsi.c \
    $(LOCAL_DIR)/aml-mipi-phy.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
