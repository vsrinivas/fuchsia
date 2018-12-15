# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := banjo

MODULE_PACKAGE := banjo

MODULE_BANJO_LIBRARY := ddk.protocol.usb.hci

MODULE_BANJO_NAME := usb/hci

MODULE_BANJO_DEPS := \
    system/banjo/ddk-driver \
    system/banjo/zircon-hw-usb \
    system/banjo/zircon-hw-usb-hub \
    system/banjo/ddk-protocol-usb-request \
    system/banjo/ddk-protocol-usb-hub \
    system/banjo/ddk-protocol-usb-bus \

MODULE_SRCS += $(LOCAL_DIR)/usb-hci.banjo

include make/module.mk

