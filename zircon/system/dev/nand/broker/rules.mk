# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_NAME := nand-broker

MODULE_SRCS := \
    $(LOCAL_DIR)/broker.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-nand \
    system/fidl/fuchsia-hardware-nand \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-nand \

include make/module.mk

# Unit tests:

MODULE := $(LOCAL_DIR).test

MODULE_NAME := nand-test

MODULE_TYPE := usertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS += \
    $(TEST_DIR)/broker-test.cpp \
    $(TEST_DIR)/main.cpp \
    $(TEST_DIR)/parent.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/devmgr-integration-test \
    system/ulib/devmgr-launcher \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/zxtest \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/ramdevice-client \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-device \
    system/fidl/fuchsia-nand \
    system/fidl/fuchsia-hardware-nand \

include make/module.mk

MODULE := $(LOCAL_DIR).nandpart_test

MODULE_NAME := nandpart-broker-test

MODULE_TYPE := usertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS += \
    $(TEST_DIR)/broker-test.cpp \
    $(TEST_DIR)/nandpart-main.cpp \
    $(TEST_DIR)/parent.cpp \

MODULE_STATIC_LIBS := \
	system/ulib/devmgr-integration-test \
	system/ulib/devmgr-launcher \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/zxtest \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/ramdevice-client \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-device \
    system/fidl/fuchsia-nand \
    system/fidl/fuchsia-hardware-nand \

include make/module.mk
