# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_NAME := blobfs

MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/bitmap \
    system/ulib/blobfs \
    system/ulib/block-client \
    system/ulib/digest \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/trace-provider \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/cksum \
    third_party/ulib/lz4 \
    third_party/ulib/uboringssl \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \

include make/module.mk
