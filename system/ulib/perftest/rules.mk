# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/results.cpp \
    $(LOCAL_DIR)/runner.cpp \

MODULE_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/c \
    system/ulib/fbl \
    system/ulib/trace \
    system/ulib/trace-engine \
    system/ulib/trace-provider \
    system/ulib/unittest \
    system/ulib/zircon \
    system/ulib/zx \

MODULE_PACKAGE := src

include make/module.mk
