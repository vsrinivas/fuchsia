#Copyright 2018 The Fuchsia Authors.All rights reserved.
#Use of this source code is governed by a BSD - style license that can be
#found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
LOCAL_INC := $(LOCAL_DIR)/include/lib/async-testutils

MODULE := $(LOCAL_DIR)
MODULE_NAME := async-testutils

MODULE_TYPE := userlib

MODULE_SRCS += \
        $(LOCAL_DIR)/dispatcher_stub.cpp \
        $(LOCAL_DIR)/test_loop.cpp \
        $(LOCAL_DIR)/test_loop_dispatcher.cpp

MODULE_STATIC_LIBS := \
        system/ulib/async.cpp \
        system/ulib/async \
        system/ulib/zx \
        system/ulib/zxcpp \
        system/ulib/fbl

MODULE_LIBS := \
        system/ulib/async.default \
        system/ulib/c \
        system/ulib/zircon \
        system/ulib/fdio \

MODULE_PACKAGE := src

include make/module.mk
