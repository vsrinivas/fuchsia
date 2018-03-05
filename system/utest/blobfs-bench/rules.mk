# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := blobfs-bench-test

MODULE_SRCS := \
    $(LOCAL_DIR)/blobfs-bench.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/digest \
    third_party/ulib/uboringssl \
    system/ulib/zxcpp \
    system/ulib/fbl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/unittest \

include make/module.mk
