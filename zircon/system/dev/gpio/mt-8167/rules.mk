# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/mt8167-gpio.cpp \
    $(LOCAL_DIR)/binding.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/mmio \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
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
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-gpioimpl \
    system/banjo/ddk-protocol-platform-bus \
    system/banjo/ddk-protocol-platform-device \

MODULE_HEADER_DEPS := system/dev/lib/mt8167

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_NAME := mtk-gpio-test

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/mt8167-gpio-test.cpp \
    $(LOCAL_DIR)/mt8167-gpio.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/mt8167 \
    system/dev/lib/fake_ddk \
    system/dev/lib/mock-mmio-reg \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
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
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-gpioimpl \
    system/banjo/ddk-protocol-platform-bus \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk
