# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# FIDL compiler host tests.
#

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hosttest

MODULE_NAME := fidl-compiler-test

MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/max_bytes_tests.cpp \
    $(LOCAL_DIR)/max_handle_tests.cpp \

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/unittest/include \

MODULE_HOST_LIBS := \
    system/host/fidl \
    system/ulib/pretty.hostlib \
    system/ulib/unittest.hostlib \


include make/module.mk
