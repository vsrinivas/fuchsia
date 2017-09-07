# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_NAME := blobstore

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/blobstore.cpp \
    $(LOCAL_DIR)/blobstore-common.cpp \
    $(LOCAL_DIR)/blobstore-ops.cpp \
    $(LOCAL_DIR)/blobstore-check.cpp \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/rpc.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fs \
    system/ulib/async \
    system/ulib/async.loop \
    system/ulib/block-client \
    system/ulib/digest \
    third_party/ulib/cryptolib \
    system/ulib/mx \
    system/ulib/mxcpp \
    system/ulib/fbl \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/bitmap \

include make/module.mk

# host blobstore tool

MODULE := $(LOCAL_DIR).host

MODULE_NAME := blobstore

MODULE_TYPE := hostapp

MODULE_SRCS := \
    $(LOCAL_DIR)/blobstore-common.cpp \
    $(LOCAL_DIR)/main.cpp \
    system/ulib/bitmap/raw-bitmap.cpp \
    system/ulib/digest/digest.cpp \
    system/ulib/digest/merkle-tree.cpp \
    system/ulib/fs/vfs.cpp \
    third_party/ulib/cryptolib/cryptolib.c \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Ithird_party/ulib/cryptolib/include \
    -Isystem/ulib/bitmap/include \
    -Isystem/ulib/digest/include \
    -Isystem/ulib/digest/include \
    -Isystem/ulib/mxcpp/include \
    -Isystem/ulib/mxio/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fs/include \

MODULE_DEFINES := DISABLE_THREAD_ANNOTATIONS

include make/module.mk
