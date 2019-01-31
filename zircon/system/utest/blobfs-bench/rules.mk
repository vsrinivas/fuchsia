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
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/blobfs \
    system/ulib/digest \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/fs-test-utils \
    system/ulib/fvm \
    system/ulib/fzl \
    system/ulib/gpt \
    system/ulib/memfs \
    system/ulib/memfs.cpp \
    system/ulib/perftest \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/trace-provider \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/uboringssl \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/fs-management \
    system/ulib/trace-engine \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-hardware-ramdisk \
    system/fidl/fuchsia-io \
    system/fidl/fuchsia-mem \

include make/module.mk
