# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS += \
    $(LOCAL_DIR)/heap.cpp \
    $(LOCAL_DIR)/scanner.cpp \
    $(LOCAL_DIR)/snapshot.cpp \

MODULE_HEADER_DEPS := \
    system/ulib/zircon-internal \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/syslog \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_PACKAGE := src

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_NAME := inspect-test

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := \
    $(TEST_DIR)/main.cpp \
    $(TEST_DIR)/heap_tests.cpp \
    $(TEST_DIR)/scanner_tests.cpp \
    $(TEST_DIR)/snapshot_tests.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/inspect \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio  \
    system/ulib/unittest \
    system/ulib/zircon \
    system/ulib/unittest \

include make/module.mk
