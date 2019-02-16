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
    $(LOCAL_DIR)/compression/lz4.cpp \
    $(LOCAL_DIR)/compression/zstd.cpp \
    $(LOCAL_DIR)/extent-reserver.cpp \
    $(LOCAL_DIR)/fsck.cpp \
    $(LOCAL_DIR)/iterator/allocated-extent-iterator.cpp \
    $(LOCAL_DIR)/iterator/block-iterator.cpp \
    $(LOCAL_DIR)/iterator/vector-extent-iterator.cpp \
    $(LOCAL_DIR)/node-reserver.cpp \

# Sources common between target and tests.
COMMON_TARGET_SRCS := \
    $(COMMON_SRCS) \
    $(LOCAL_DIR)/allocator.cpp \
    $(LOCAL_DIR)/blob.cpp \
    $(LOCAL_DIR)/blobfs.cpp \
    $(LOCAL_DIR)/blob-cache.cpp \
    $(LOCAL_DIR)/cache-node.cpp \
    $(LOCAL_DIR)/compression/blob-compressor.cpp \
    $(LOCAL_DIR)/directory.cpp \
    $(LOCAL_DIR)/iterator/node-populator.cpp \
    $(LOCAL_DIR)/journal.cpp \
    $(LOCAL_DIR)/metrics.cpp \
    $(LOCAL_DIR)/writeback.cpp \

TARGET_MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/bitmap \
    system/ulib/block-client \
    system/ulib/cobalt-client \
    system/ulib/digest \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fidl-async \
    system/ulib/fidl-utils \
    system/ulib/fvm \
    system/ulib/fs \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/cksum \
    third_party/ulib/lz4 \
    third_party/ulib/uboringssl \
    third_party/ulib/zstd \

TARGET_MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/zircon \

TARGET_MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-cobalt \
    system/fidl/fuchsia-device \
    system/fidl/fuchsia-io \
    system/fidl/fuchsia-mem \
    system/fidl/fuchsia-blobfs \

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
    $(TEST_DIR)/allocated-extent-iterator-test.cpp \
    $(TEST_DIR)/allocator-test.cpp \
    $(TEST_DIR)/blob-cache-test.cpp \
    $(TEST_DIR)/compressor-test.cpp \
    $(TEST_DIR)/extent-reserver-test.cpp \
    $(TEST_DIR)/get-allocated-regions-test.cpp \
    $(TEST_DIR)/journal-test.cpp \
    $(TEST_DIR)/node-populator-test.cpp \
    $(TEST_DIR)/node-reserver-test.cpp \
    $(TEST_DIR)/utils.cpp \
    $(TEST_DIR)/vector-extent-iterator-test.cpp \
    $(TEST_DIR)/writeback-test.cpp \

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

MODULE_HOST_LIBS := \
    system/ulib/fs-host \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/digest/include \
    -Ithird_party/ulib/lz4/include \
    -Ithird_party/ulib/uboringssl/include \
    -Ithird_party/ulib/zstd/include \
    -Isystem/ulib/cobalt-client/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/bitmap/include \
    -Ithird_party/ulib/cksum/include \

include make/module.mk
