# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/fixture.cpp \
    $(LOCAL_DIR)/fuzzer-fixture.cpp \
    $(LOCAL_DIR)/test-fuzzer.cpp \

MODULE_SRCS += \
    $(LOCAL_DIR)/fuzzer.cpp \
    $(LOCAL_DIR)/path.cpp \
    $(LOCAL_DIR)/string-list.cpp \
    $(LOCAL_DIR)/string-map.cpp \

MODULE_NAME := fuzz-utils-test

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/unittest \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fuzz-utils \
    system/ulib/task-utils \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-sysinfo

include make/module.mk
