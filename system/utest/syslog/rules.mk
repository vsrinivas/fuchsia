# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/syslog_tests.c \
    $(LOCAL_DIR)/syslog_socket_tests.cpp \

MODULE_NAME := syslog-test

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/syslog \

include make/module.mk
