#Copyright 2017 The Fuchsia Authors.All rights reserved.
#Use of this source code is governed by a BSD - style license that can be
#found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/usb-device.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/magenta \
    system/ulib/c


# Set default configuration here, rather than relying on usbctl to do it
MODULE_DEFINES := USB_DEVICE_VID=0x18D1 \
                  USB_DEVICE_PID=0xA020 \
                  USB_DEVICE_MANUFACTURER=\"Zircon\" \
                  USB_DEVICE_PRODUCT=\"CDC-Ethernet\" \
                  USB_DEVICE_SERIAL=\"0123456789ABCDEF\" \
                  USB_DEVICE_FUNCTIONS=\"cdc\"

include make/module.mk
