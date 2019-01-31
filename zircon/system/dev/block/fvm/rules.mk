# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

SHARED_SRCS := \
    $(LOCAL_DIR)/fvm.c \
    $(LOCAL_DIR)/fvm.cpp \
    $(LOCAL_DIR)/slice-extent.cpp \
    $(LOCAL_DIR)/vpartition.cpp \

SHARED_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/digest \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/fzl \
    system/ulib/fvm \
    system/ulib/gpt \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/uboringssl \

SHARED_MODULE_LIBS := \
    system/ulib/c \
    system/ulib/driver \
    system/ulib/zircon \

SHARED_BANJO_LIBS := \
    system/banjo/ddk-protocol-block \
    system/banjo/ddk-protocol-block-partition \
    system/banjo/ddk-protocol-block-volume \

MODULE_SRCS := $(SHARED_SRCS)

MODULE_STATIC_LIBS := $(SHARED_STATIC_LIBS)

MODULE_LIBS := $(SHARED_MODULE_LIBS)

MODULE_BANJO_LIBS := $(SHARED_BANJO_LIBS)

include make/module.mk

# Unit Tests

MODULE := $(LOCAL_DIR).test

MODULE_NAME := fvm-driver-unittests

MODULE_TYPE := usertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := $(SHARED_SRCS) \
    $(TEST_DIR)/slice-extent-test.cpp \
    $(TEST_DIR)/main.cpp \

MODULE_STATIC_LIBS := \
    $(SHARED_STATIC_LIBS) \
    system/ulib/pretty \
    system/ulib/unittest \

MODULE_LIBS := $(SHARED_MODULE_LIBS)

MODULE_BANJO_LIBS := $(SHARED_BANJO_LIBS)

MODULE_COMPILEFLAGS := \
    -I$(LOCAL_DIR)\

include make/module.mk
