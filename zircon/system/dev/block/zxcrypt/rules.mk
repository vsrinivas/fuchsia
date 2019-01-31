# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/device.cpp \
    $(LOCAL_DIR)/extra.cpp \
    $(LOCAL_DIR)/worker.cpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/crypto \
    system/ulib/driver \
    system/ulib/zxcrypt \

MODULE_STATIC_LIBS := \
    system/ulib/bitmap \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/pretty \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-block \
    system/banjo/ddk-protocol-block-partition \
    system/banjo/ddk-protocol-block-volume \

MODULE_COMPILEFLAGS := -fsanitize=integer-divide-by-zero,signed-integer-overflow -fsanitize-undefined-trap-on-error

include make/module.mk
