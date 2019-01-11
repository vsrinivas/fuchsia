#Copyright 2017 The Fuchsia Authors.All rights reserved.
#Use of this source code is governed by a BSD - style license that can be
#found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/usb-function.cpp \
    $(LOCAL_DIR)/usb-peripheral.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_FIDL_LIBS := system/fidl/fuchsia-hardware-usb-peripheral

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb \
    system/banjo/ddk-protocol-usb-dci \
    system/banjo/ddk-protocol-usb-function \
    system/banjo/ddk-protocol-usb-modeswitch \
    system/banjo/ddk-protocol-usb-request \

# Set default configuration here, rather than relying on usbctl to do it
MODULE_DEFINES := USB_DEVICE_VID=0x18D1 \
                  USB_DEVICE_PID=0xA020 \
                  USB_DEVICE_MANUFACTURER=\"Zircon\" \
                  USB_DEVICE_PRODUCT=\"CDC-Ethernet\" \
                  USB_DEVICE_SERIAL=\"0123456789ABCDEF\" \
                  USB_DEVICE_FUNCTIONS=\"cdc\"

include make/module.mk
