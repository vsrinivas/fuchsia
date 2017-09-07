# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

reader_tests := \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/reader_tests.cpp \
    $(LOCAL_DIR)/records_tests.cpp

# Userspace tests.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS := $(reader_tests)

MODULE_NAME := trace-reader-test

MODULE_STATIC_LIBS := \
    system/ulib/trace-reader \
    system/ulib/trace-engine \
    system/ulib/mx \
    system/ulib/mxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/unittest

include make/module.mk

# Host tests.

MODULE := $(LOCAL_DIR).hostapp

MODULE_TYPE := hostapp

MODULE_SRCS := $(reader_tests)

MODULE_NAME := trace-reader-test

MODULE_HOST_LIBS := \
    system/ulib/trace-reader.hostlib \
    system/ulib/fbl.hostlib \
    system/ulib/unittest.hostlib \
    system/ulib/pretty.hostlib

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/trace-engine/include \
    -Isystem/ulib/trace-reader/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/unittest/include \

include make/module.mk

# Clear out local variables.

reader_tests :=
