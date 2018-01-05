# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/minfs \
    system/ulib/fs \
    system/ulib/async \
    system/ulib/async.loop \
    system/ulib/block-client \
    system/ulib/trace-provider \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/bitmap \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/zircon \

include make/module.mk

# host minfs tool

MODULE := $(LOCAL_DIR).host

MODULE_NAME := minfs

MODULE_TYPE := hostapp

MODULE_SRCS := \
    $(LOCAL_DIR)/main-host.cpp \

MODULE_HOST_SRCS := \
    $(LOCAL_DIR)/main-host.cpp \
    system/ulib/bitmap/raw-bitmap.cpp \
    system/ulib/fs/vfs.cpp \
    system/ulib/fs/vnode.cpp \

MODULE_HOST_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/bitmap/include \
    -Isystem/ulib/zxcpp/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/minfs/include \

MODULE_COMPILEFLAGS := $(MODULE_HOST_COMPILEFLAGS)

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/minfs.hostlib

MODULE_DEFINES += DISABLE_THREAD_ANNOTATIONS

include make/module.mk
