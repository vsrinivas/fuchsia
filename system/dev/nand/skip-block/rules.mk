# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/logical-to-physical-map.cpp \
    $(LOCAL_DIR)/skip-block.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/driver \
    system/ulib/zircon \

include make/module.mk

# Unit tests.

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_NAME := skip-block-test

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := \
    $(LOCAL_DIR)/logical-to-physical-map.cpp \
    $(TEST_DIR)/logical-to-physical-map-test.cpp \
    $(TEST_DIR)/main.cpp \

MODULE_COMPILEFLAGS := \
    -I$(LOCAL_DIR) \
    -DTEST \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/pretty \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
