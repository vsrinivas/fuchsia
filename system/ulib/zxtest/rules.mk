# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/event-broadcaster.cpp \
    $(LOCAL_DIR)/reporter.cpp \
    $(LOCAL_DIR)/runner.cpp \
    $(LOCAL_DIR)/test-case.cpp \
    $(LOCAL_DIR)/test.cpp \
    $(LOCAL_DIR)/test-info.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \

MODULE_PACKAGE := src

include make/module.mk

#
# zxtest sanity tests
#

MODULE := $(LOCAL_DIR).test

MODULE_NAME := zxtest-test

MODULE_TYPE := usertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := \
    $(TEST_DIR)/event-broadcaster_test.cpp \
    $(TEST_DIR)/runner_test.cpp \
    $(TEST_DIR)/test-case_test.cpp \
    $(TEST_DIR)/test-info_test.cpp \
    $(TEST_DIR)/test_test.cpp \
    $(TEST_DIR)/main.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/zxtest \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c \

include make/module.mk
