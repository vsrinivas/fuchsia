# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/fzl-test.cpp \
    $(LOCAL_DIR)/mapped-vmo.cpp \
    $(LOCAL_DIR)/vmo-pool-tests.cpp \
    $(LOCAL_DIR)/vmo-probe.cpp \
    $(LOCAL_DIR)/vmo-vmar-tests.cpp \

MODULE_NAME := libfzl-test

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
