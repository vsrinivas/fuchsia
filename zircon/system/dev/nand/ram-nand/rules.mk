# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/bind.cpp \
    $(LOCAL_DIR)/ram-nand.cpp \
    $(LOCAL_DIR)/ram-nand-ctl.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/driver \
    system/ulib/zircon \

MODULE_FIDL_LIBS := system/fidl/fuchsia-hardware-nand

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-nand \

include make/module.mk

# Unit tests:

MODULE := $(LOCAL_DIR).test

MODULE_NAME := ram-nand-test

MODULE_TYPE := usertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS += \
    $(LOCAL_DIR)/ram-nand.cpp \
x    $(TEST_DIR)/ram-nand.cpp \
    $(TEST_DIR)/ram-nand-ctl.cpp \

MODULE_COMPILEFLAGS := -I$(LOCAL_DIR)

MODULE_STATIC_LIBS := \
    system/dev/lib/fake_ddk \
    system/ulib/devmgr-integration-test \
    system/ulib/devmgr-launcher \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/ramdevice-client \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_FIDL_LIBS := system/fidl/fuchsia-hardware-nand

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-nand \

include make/module.mk
