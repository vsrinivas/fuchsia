# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/hikey960.c \
    $(LOCAL_DIR)/hikey960-devices.c \
    $(LOCAL_DIR)/hikey960-i2c.c \

MODULE_STATIC_LIBS := \
    system/dev/soc/hi3660 \
    system/dev/gpio/pl061 \
    system/ulib/ddk \
    system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

include make/module.mk

MODULE := $(LOCAL_DIR).i2c-test.c

MODULE_NAME := hi3660-i2c-test

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/hikey960-i2c-test.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

include make/module.mk
