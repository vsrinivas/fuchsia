# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/pseudo-dir-tests.cpp \
    $(LOCAL_DIR)/pseudo-file-tests.cpp \
    $(LOCAL_DIR)/remote-dir-tests.cpp \
    $(LOCAL_DIR)/service-tests.cpp \
    $(LOCAL_DIR)/teardown-tests.cpp \
    $(LOCAL_DIR)/vmo-file-tests.cpp \
    $(LOCAL_DIR)/main.c

MODULE_NAME := fs-vnode-test

MODULE_STATIC_LIBS := \
    system/ulib/fidl \
    system/ulib/fs \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io

include make/module.mk
