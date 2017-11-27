# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := fs

MODULE_NAME := blobfs-test

MODULE_SRCS := \
    $(LOCAL_DIR)/blobfs.cpp

MODULE_STATIC_LIBS := \
    system/ulib/fvm \
    system/ulib/digest \
    system/ulib/fs \
    system/ulib/gpt \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/blobfs \
    third_party/ulib/uboringssl \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/c \
    system/ulib/fs-management \
    system/ulib/zircon \
    system/ulib/unittest \

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/blobfs/include \

include make/module.mk
