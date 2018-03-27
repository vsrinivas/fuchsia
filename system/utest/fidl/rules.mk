# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/abi_tests.cpp \
    $(LOCAL_DIR)/cpp_types_tests.cpp \
    $(LOCAL_DIR)/decoding_tests.cpp \
    $(LOCAL_DIR)/encoding_tests.cpp \
    $(LOCAL_DIR)/fidl_coded_types.cpp \
    $(LOCAL_DIR)/formatting_tests.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/message_tests.cpp \
    $(LOCAL_DIR)/validating_tests.cpp \

MODULE_NAME := fidl-test

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
