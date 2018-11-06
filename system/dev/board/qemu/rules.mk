# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
TEST_DIR := $(LOCAL_DIR)/test

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/qemu-bus.c \
    $(LOCAL_DIR)/qemu-test.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk

# Below we have rules for four test drivers for testing platform bus features

MODULE := $(LOCAL_DIR).test-parent

MODULE_NAME := qemu-test-parent

MODULE_TYPE := driver

MODULE_SRCS := \
    $(TEST_DIR)/parent.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk

MODULE := $(LOCAL_DIR).test-child-1

MODULE_NAME := qemu-test-child-1

MODULE_TYPE := driver

MODULE_SRCS := \
    $(TEST_DIR)/child-1.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk

MODULE := $(LOCAL_DIR).test-child-2

MODULE_NAME := qemu-test-child-2

MODULE_TYPE := driver

MODULE_SRCS := \
    $(TEST_DIR)/child-2.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk

MODULE := $(LOCAL_DIR).test-child-3

MODULE_NAME := qemu-test-child-3

MODULE_TYPE := driver

MODULE_SRCS := \
    $(TEST_DIR)/child-3.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk
