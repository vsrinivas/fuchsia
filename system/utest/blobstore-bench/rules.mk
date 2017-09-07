# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := blobstore-bench-test

MODULE_SRCS := \
    $(LOCAL_DIR)/blobstore-bench.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/digest \
    third_party/ulib/cryptolib \
    system/ulib/mxcpp \
    system/ulib/fbl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/unittest \

include make/module.mk
