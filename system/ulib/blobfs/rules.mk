# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_USERTEST_GROUP := fs

MODULE_NAME := blobfs

MODULE_COMPILEFLAGS += -fvisibility=hidden

# Sources common between host, target, and tests.
COMMON_SRCS := \
    $(LOCAL_DIR)/common.cpp \
    $(LOCAL_DIR)/fsck.cpp \
    $(LOCAL_DIR)/extent-reserver.cpp \
    $(LOCAL_DIR)/lz4.cpp \
    $(LOCAL_DIR)/node-reserver.cpp \

# Sources common between target and tests.
COMMON_TARGET_SRCS := \
    $(COMMON_SRCS) \
    $(LOCAL_DIR)/blobfs.cpp \
    $(LOCAL_DIR)/journal.cpp \
    $(LOCAL_DIR)/metrics.cpp \
    $(LOCAL_DIR)/rpc.cpp \
    $(LOCAL_DIR)/vnode.cpp \
    $(LOCAL_DIR)/writeback.cpp \

TARGET_MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/bitmap \
    system/ulib/block-client \
    system/ulib/digest \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/cksum \
    third_party/ulib/lz4 \
    third_party/ulib/uboringssl \

TARGET_MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/zircon \

TARGET_MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \

# target blobfs lib
MODULE_SRCS := \
    $(COMMON_TARGET_SRCS) \

MODULE_STATIC_LIBS := $(TARGET_MODULE_STATIC_LIBS)

MODULE_LIBS := $(TARGET_MODULE_LIBS)

MODULE_FIDL_LIBS := $(TARGET_MODULE_FIDL_LIBS)

include make/module.mk

# target blobfs tests

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_NAME := blobfs-unit-tests

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := \
    $(COMMON_TARGET_SRCS) \
    $(TEST_DIR)/extent-reserver-test.cpp \
    $(TEST_DIR)/main.cpp \
    $(TEST_DIR)/node-reserver-test.cpp \

MODULE_STATIC_LIBS := $(TARGET_MODULE_STATIC_LIBS)

MODULE_LIBS := \
    $(TARGET_MODULE_LIBS) \
    system/ulib/unittest \

MODULE_FIDL_LIBS := $(TARGET_MODULE_FIDL_LIBS)

MODULE_COMPILEFLAGS := \
    -I$(LOCAL_DIR) \

include make/module.mk

# host blobfs lib

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS := \
    $(COMMON_SRCS) \
    $(LOCAL_DIR)/host.cpp \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/digest/include \
    -Ithird_party/ulib/lz4/include \
    -Ithird_party/ulib/uboringssl/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/bitmap/include \
    -Ithird_party/ulib/cksum/include \

include make/module.mk
