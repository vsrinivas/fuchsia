# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)
MODULE_SO_NAME := $(LOCAL_DIR)
MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/volume.cpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/crypto \
    system/ulib/driver \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    third_party/ulib/uboringssl \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/pretty \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp

MODULE_COMPILEFLAGS := -fsanitize=integer-divide-by-zero,signed-integer-overflow -fsanitize-undefined-trap-on-error

include make/module.mk
