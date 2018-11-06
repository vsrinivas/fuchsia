# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/a113-clocks.c \
    $(LOCAL_DIR)/aml-usb-phy-v2.c \
    $(LOCAL_DIR)/s905d2-mali.c \
    $(LOCAL_DIR)/s905d2-hiu.c \
    $(LOCAL_DIR)/s905d2-pll-rates.c \
    $(LOCAL_DIR)/aml-tdm-audio.cpp \
    $(LOCAL_DIR)/aml-pdm-audio.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/sync \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk
