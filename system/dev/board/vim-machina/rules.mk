# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

SRC_DIR := system/dev/board/vim

MODULE_SRCS += \
    $(LOCAL_DIR)/vim.c \
    $(SRC_DIR)/vim-clk.c \
    $(SRC_DIR)/vim-eth.c \
    $(SRC_DIR)/vim-gpio.c \
    $(SRC_DIR)/vim-i2c.c \
    $(SRC_DIR)/vim-sd-emmc.c \
    $(SRC_DIR)/vim-sdio.c \
    $(SRC_DIR)/vim-uart.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/sync \
    system/dev/lib/broadcom \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon

MODULE_HEADER_DEPS := \
    system/dev/lib/amlogic \

MODULE_COMPILEFLAGS += -I$(SRC_DIR)

include make/module.mk
