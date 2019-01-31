# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/usb-bus.cpp \
    $(LOCAL_DIR)/usb-device.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/sync \
    system/ulib/utf_conversion \
    system/dev/lib/operation \
    system/dev/lib/usb \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-hardware-usb-device \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb \
    system/banjo/ddk-protocol-usb-bus \
    system/banjo/ddk-protocol-usb-composite \
    system/banjo/ddk-protocol-usb-hci \
    system/banjo/ddk-protocol-usb-hub \
    system/banjo/ddk-protocol-usb-request \

include make/module.mk
