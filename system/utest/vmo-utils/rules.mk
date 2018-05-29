# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/vmo_vmar_tests.cpp \
    $(LOCAL_DIR)/vmo_pool_tests.cpp \
    $(LOCAL_DIR)/vmo_probe.cpp \

MODULE_NAME := vmo-utils-test

MODULE_STATIC_LIBS := \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/vmo-utils \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
