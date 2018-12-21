# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/vim.cpp \
    $(LOCAL_DIR)/vim-gpio.cpp\
    $(LOCAL_DIR)/vim-i2c.cpp \
    $(LOCAL_DIR)/vim-mali.cpp \
    $(LOCAL_DIR)/vim-uart.cpp \
    $(LOCAL_DIR)/vim-usb.cpp \
    $(LOCAL_DIR)/vim-sd-emmc.cpp \
    $(LOCAL_DIR)/vim-sdio.cpp \
    $(LOCAL_DIR)/vim-eth.cpp \
    $(LOCAL_DIR)/vim-thermal.cpp \
    $(LOCAL_DIR)/vim-display.cpp \
    $(LOCAL_DIR)/vim-video.cpp \
    $(LOCAL_DIR)/vim-led2472g.cpp \
    $(LOCAL_DIR)/vim-rtc.cpp \
    $(LOCAL_DIR)/vim-canvas.cpp \
    $(LOCAL_DIR)/vim-clk.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/sync \
    system/dev/lib/broadcom \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-ethernet \
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-gpioimpl \
    system/banjo/ddk-protocol-iommu \
    system/banjo/ddk-protocol-platform-bus \
    system/banjo/ddk-protocol-platform-device \
    system/banjo/ddk-protocol-scpi \
    system/banjo/ddk-protocol-serial \

MODULE_HEADER_DEPS := \
    system/dev/lib/amlogic \

include make/module.mk
