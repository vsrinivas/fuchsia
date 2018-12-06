# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/usb-bus.c \
    $(LOCAL_DIR)/usb-device.c \
    $(LOCAL_DIR)/util.c \

MODULE_STATIC_LIBS := \
    system/dev/lib/usb \
    system/ulib/ddk \
    system/ulib/fidl \
    system/ulib/sync \
    system/ulib/utf_conversion \
    system/dev/lib/usb \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_FIDL_LIBS := \
    system/fidl/zircon-usb-device \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb-bus \
    system/banjo/ddk-protocol-usb-composite \
    system/banjo/ddk-protocol-usb-hub \

include make/module.mk
