# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := fvm-test

MODULE_SRCS := \
    $(LOCAL_DIR)/fvm.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/block-client \
    system/ulib/fvm \
    system/ulib/fs \
    system/ulib/gpt \
    system/ulib/digest \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/sync \
    third_party/ulib/uboringssl \
    system/ulib/minfs \
    system/ulib/bitmap

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fs-management \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/unittest \

include make/module.mk
