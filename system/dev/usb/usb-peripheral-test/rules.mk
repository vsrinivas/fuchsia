# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).driver

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/driver.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/sync \
    system/dev/lib/usb-old \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb-function \
    system/banjo/ddk-protocol-usb-request \

MODULE_HEADER_DEPS := \
    system/ulib/inet6 \

include make/module.mk

ifeq ($(HOST_PLATFORM),linux)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hosttest

MODULE_SRCS += \
    $(LOCAL_DIR)/test.cpp \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/unittest.hostlib \
    third_party/ulib/usbhost \

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/unittest/include \

MODULE_PACKAGE := bin

include make/module.mk

endif # linux
