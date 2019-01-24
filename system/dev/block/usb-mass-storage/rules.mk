# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/usb-mass-storage.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/usb \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/ftl \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-block \
    system/banjo/ddk-protocol-usb \
    system/banjo/ddk-protocol-usb-composite \
    system/banjo/ddk-protocol-usb-request \

include make/module.mk

MODULE := $(LOCAL_DIR).test
MODULE_TYPE := usertest
MODULE_NAME := ums-block-test
MODULE_SRCS := \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/tests/block.cpp \
    $(LOCAL_DIR)/tests/main.c \

MODULE_STATIC_LIBS := system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/dev/lib/fake_ddk \
    system/dev/lib/usb \
    system/ulib/sync \
    system/ulib/zxcpp \

MODULE_LIBS := system/ulib/unittest \
    system/ulib/fdio \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \
    system/dev/lib/fake-bti \


MODULE_COMPILEFLAGS += \
    -Isystem/ulib/fit/include \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-block \
    system/banjo/ddk-protocol-usb \
    system/banjo/ddk-protocol-usb-composite \
    system/banjo/ddk-protocol-usb-request \

MODULE_PACKAGE := src

include make/module.mk

MODULE := $(LOCAL_DIR).dev.test
MODULE_TYPE := usertest
MODULE_NAME := ums-test
MODULE_SRCS := \
    $(LOCAL_DIR)/usb-mass-storage.cpp \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/tests/usb-mass-storage.cpp \
    $(LOCAL_DIR)/tests/main.c \

MODULE_STATIC_LIBS := system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/dev/lib/fake_ddk \
    system/dev/lib/usb \
    system/ulib/sync \
    system/ulib/zxcpp \

MODULE_LIBS := system/ulib/unittest \
    system/ulib/fdio \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \
    system/dev/lib/fake-bti \

MODULE_COMPILEFLAGS += \
    -Isystem/ulib/fit/include \
    -DUNITTEST \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-block \
    system/banjo/ddk-protocol-usb \
    system/banjo/ddk-protocol-usb-composite \
    system/banjo/ddk-protocol-usb-request \

MODULE_PACKAGE := src

include make/module.mk
