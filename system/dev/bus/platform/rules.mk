# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_NAME := platform-bus

MODULE_SRCS := \
    $(LOCAL_DIR)/platform-bus.c \
    $(LOCAL_DIR)/platform-device.c \
    $(LOCAL_DIR)/platform-i2c.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).proxy

MODULE_TYPE := driver

MODULE_NAME := platform-bus.proxy

MODULE_SRCS := \
    $(LOCAL_DIR)/platform-proxy.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
