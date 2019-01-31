# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

COMMON_SRCS := \
    $(LOCAL_DIR)/allocator.cpp \
    $(LOCAL_DIR)/bcache.cpp \
    $(LOCAL_DIR)/fsck.cpp \
    $(LOCAL_DIR)/inode-manager.cpp \
    $(LOCAL_DIR)/minfs.cpp \
    $(LOCAL_DIR)/superblock.cpp \
    $(LOCAL_DIR)/transaction-limits.cpp \
    $(LOCAL_DIR)/vnode.cpp \
    $(LOCAL_DIR)/writeback.cpp \

# minfs implementation
MODULE_SRCS := \
    $(COMMON_SRCS)

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/bitmap \
    system/ulib/block-client \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fidl-async \
    system/ulib/fidl-utils \
    system/ulib/fs \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/zircon-internal \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \
    system/fidl/fuchsia-mem \
    system/fidl/fuchsia-minfs \

include make/module.mk

MODULE_HOST_SRCS := \
    $(COMMON_SRCS) \
    $(LOCAL_DIR)/host.cpp \
    system/ulib/bitmap/raw-bitmap.cpp \

MODULE_HOST_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/bitmap/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/fzl/include \
    -Isystem/ulib/zxcpp/include \

# host minfs lib

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS := $(MODULE_HOST_SRCS)

MODULE_COMPILEFLAGS := $(MODULE_HOST_COMPILEFLAGS)

MODULE_HEADER_DEPS += system/ulib/zircon-internal

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/fs.hostlib \

include make/module.mk
