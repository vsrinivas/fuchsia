# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/gauss.c \
    $(LOCAL_DIR)/gauss-audio.c \
    $(LOCAL_DIR)/gauss-clk.c \
    $(LOCAL_DIR)/gauss-gpio.c \
    $(LOCAL_DIR)/gauss-i2c.c \
    $(LOCAL_DIR)/gauss-pcie.c \
    $(LOCAL_DIR)/gauss-usb.c \

MODULE_STATIC_LIBS := \
    system/dev/soc/amlogic \
    system/ulib/ddk \
    system/ulib/sync

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk

MODULE := $(LOCAL_DIR).i2c-test

MODULE_NAME := gauss-i2c-test

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/gauss-i2c-test.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk

ifeq (PLEASE_DISCUSS_WITH_SWETLAND,)
MODULE := $(LOCAL_DIR).led

MODULE_NAME := gauss-led

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/gauss-led.c

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

include make/module.mk
endif
