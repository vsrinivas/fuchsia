# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).fidl
MODULE_TYPE := fidl
MODULE_FIDL_LIBRARY := fuchsia.device.mock
MODULE_SRCS += $(LOCAL_DIR)/mock-device.fidl

include make/module.mk

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SO_INSTALL_NAME := driver/test/mock-device.so

MODULE_SRCS := \
  $(LOCAL_DIR)/device.cpp \
  $(LOCAL_DIR)/fidl.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_FIDL_LIBS := $(LOCAL_DIR).fidl
MODULE_BANJO_LIBS := system/banjo/ddk-protocol-test

include make/module.mk
