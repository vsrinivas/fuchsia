# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#MODULE := $(LOCAL_DIR).proxy
MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_NAME := nand

MODULE_SRCS += \
    $(LOCAL_DIR)/nand.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/operation \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/driver \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-hardware-nand \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-nand \
    system/banjo/ddk-protocol-rawnand \

include make/module.mk

# Unit tests.

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_NAME := nand-unittest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := \
    $(LOCAL_DIR)/nand.cpp \
    $(TEST_DIR)/nand-test.cpp\

MODULE_COMPILEFLAGS := \
    -I$(LOCAL_DIR) \
    -DTEST \

MODULE_STATIC_LIBS := \
    system/dev/lib/fake_ddk \
    system/dev/lib/operation \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
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
    system/fidl/fuchsia-hardware-nand \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-nand \
    system/banjo/ddk-protocol-rawnand \

include make/module.mk
