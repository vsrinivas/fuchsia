# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/elf-search-test.cpp \

MODULE_NAME := elf-search-test

MODULE_STATIC_LIBS := \
    system/ulib/zx \
    system/ulib/fbl \
    system/ulib/elf-search \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/launchpad \
    system/ulib/zircon \
    system/ulib/unittest \
    system/ulib/c

include make/module.mk
