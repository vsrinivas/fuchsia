# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/mtk-sdmmc.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/mt8167 \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/hwreg \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-block \
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-i2c \
    system/banjo/ddk-protocol-platform-device \
    system/banjo/ddk-protocol-sdmmc \

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_NAME := mtk-sdmmc-test

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/mtk-sdmmc.cpp \
    $(LOCAL_DIR)/mtk-sdmmc-test.cpp \
    $(LOCAL_DIR)/test-overrides.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/mt8167 \
    system/dev/lib/fake_ddk \
    system/dev/lib/mock_mmio_reg \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/hwreg \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-block \
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-platform-device \
    system/banjo/ddk-protocol-sdmmc \

include make/module.mk
