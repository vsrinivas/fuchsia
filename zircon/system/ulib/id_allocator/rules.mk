# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden \

COMMON_TARGET_SRCS := \
    $(LOCAL_DIR)/id_allocator.cpp \

TARGET_MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/bitmap \
    system/ulib/zx \
    system/ulib/zxcpp \

TARGET_MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \

MODULE_SRCS += \
	$(COMMON_TARGET_SRCS) \

MODULE_STATIC_LIBS := $(TARGET_MODULE_STATIC_LIBS)

MODULE_LIBS := $(TARGET_MODULE_LIBS)

MODULE_PACKAGE := src

include make/module.mk

# target id_allocator tests

MODULE := $(LOCAL_DIR)/.test

MODULE_TYPE := usertest

MODULE_NAME := id-allocator-test

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS += \
	$(COMMON_TARGET_SRCS) \
    $(TEST_DIR)/id_allocator.cpp \

MODULE_STATIC_LIBS := \
	$(TARGET_MODULE_STATIC_LIBS) \
	system/ulib/id_allocator \

MODULE_LIBS := \
	$(TARGET_MODULE_LIBS) \
	system/ulib/unittest \

MODULE_COMPILEFLAGS := \
	-I$(LOCAL_DIR) \

include make/module.mk
