# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/vim.c \
    $(LOCAL_DIR)/vim-gpio.c \
    $(LOCAL_DIR)/vim-i2c.c \
    $(LOCAL_DIR)/vim-mali.c \
    $(LOCAL_DIR)/vim-uart.c \
    $(LOCAL_DIR)/vim-usb.c \
    $(LOCAL_DIR)/vim-sd-emmc.c \
    $(LOCAL_DIR)/vim-sdio.c \
    $(LOCAL_DIR)/vim-eth.c \
    $(LOCAL_DIR)/vim-thermal.c \
    $(LOCAL_DIR)/vim-mailbox.c \
    $(LOCAL_DIR)/vim-display.c \
    $(LOCAL_DIR)/vim-video.c \
    $(LOCAL_DIR)/vim-led2472g.c \
    $(LOCAL_DIR)/vim-rtc.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/sync

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon

MODULE_HEADER_DEPS := \
    system/dev/lib/amlogic

include make/module.mk
