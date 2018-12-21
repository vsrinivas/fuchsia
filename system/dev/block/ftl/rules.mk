# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := ftl

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/bind.cpp \
    $(LOCAL_DIR)/block_device.cpp \
    $(LOCAL_DIR)/nand_driver.cpp \
    $(LOCAL_DIR)/nand_operation.cpp \
    $(LOCAL_DIR)/oob_doubler.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/ftl \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/driver \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/zircon-nand \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-badblock \
    system/banjo/ddk-protocol-nand \

include make/module.mk

# Unit tests:

MODULE := $(LOCAL_DIR).test

MODULE_NAME := ftl-test

MODULE_TYPE := usertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS += \
    $(LOCAL_DIR)/block_device.cpp \
    $(LOCAL_DIR)/nand_driver.cpp \
    $(LOCAL_DIR)/nand_operation.cpp \
    $(LOCAL_DIR)/oob_doubler.cpp \
    $(TEST_DIR)/driver-test.cpp \
    $(TEST_DIR)/main.cpp \
    $(TEST_DIR)/ftl-shell.cpp \
    $(TEST_DIR)/ftl-test.cpp \
    $(TEST_DIR)/nand_operation_test.cpp \
    $(TEST_DIR)/ndm-ram-driver.cpp \
    $(TEST_DIR)/oob_doubler_test.cpp \

    # TODO(rvargas): enable these tests:
    # $(TEST_DIR)/block_device_test.cpp \
    # $(TEST_DIR)/nand_driver_test.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/fake_ddk \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/ftl \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/zircon-nand \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-badblock \
    system/banjo/ddk-protocol-nand \

MODULE_COMPILEFLAGS := -I$(LOCAL_DIR)

include make/module.mk
