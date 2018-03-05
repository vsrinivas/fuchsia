# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).host

MODULE_NAME := blobfs

MODULE_TYPE := hostapp

MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \
    system/ulib/bitmap/raw-bitmap.cpp \
    system/ulib/fs/vfs.cpp \
    system/ulib/fs/vnode.cpp \

MODULE_HOST_LIBS := \
    third_party/ulib/uboringssl.hostlib \
    system/ulib/blobfs.hostlib \
    system/ulib/digest.hostlib \
    system/ulib/fbl.hostlib \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/bitmap/include \
    -Isystem/ulib/blobfs/include \
    -Isystem/ulib/digest/include \
    -Isystem/ulib/zxcpp/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fs/include \

MODULE_DEFINES += DISABLE_THREAD_ANNOTATIONS

include make/module.mk
