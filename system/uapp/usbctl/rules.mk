# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/usbctl.c

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/c

MODULE_STATIC_LIBS := \
    system/ulib/fidl \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-hardware-usb-peripheral \
    system/fidl/fuchsia-hardware-usb-virtual-bus \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb-modeswitch \

include make/module.mk
