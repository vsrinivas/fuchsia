# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/usb-xhci.cpp \
    $(LOCAL_DIR)/xdc.cpp \
    $(LOCAL_DIR)/xdc-transfer.cpp \
    $(LOCAL_DIR)/xhci.cpp \
    $(LOCAL_DIR)/xhci-device-manager.cpp \
    $(LOCAL_DIR)/xhci-root-hub.cpp \
    $(LOCAL_DIR)/xhci-transfer.cpp \
    $(LOCAL_DIR)/xhci-transfer-common.cpp \
    $(LOCAL_DIR)/xhci-trb.cpp \
    $(LOCAL_DIR)/xhci-util.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/hwreg \
    system/ulib/sync \
    system/ulib/xdc-server-utils \
    system/ulib/zx \
    system/dev/lib/usb-old \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_FIDL_LIBS := system/fidl/fuchsia-usb-debug

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-pci \
    system/banjo/ddk-protocol-platform-device \
    system/banjo/ddk-protocol-usb-bus \
    system/banjo/ddk-protocol-usb-hci \
    system/banjo/ddk-protocol-usb-hub \
    system/banjo/ddk-protocol-usb-request \

include make/module.mk
