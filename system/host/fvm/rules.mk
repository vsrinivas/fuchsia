# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostapp

MODULE_SRCS += \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/container/container.cpp \
    $(LOCAL_DIR)/container/fvm.cpp \
    $(LOCAL_DIR)/container/sparse.cpp \
    $(LOCAL_DIR)/format/blobfs.cpp \
    $(LOCAL_DIR)/format/format.cpp \
    $(LOCAL_DIR)/format/minfs.cpp \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Ithird_party/ulib/lz4/include \
    -Isystem/uapp/fvm/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fvm/include \
    -Isystem/ulib/digest/include \
    -Isystem/ulib/gpt/include \
    -Isystem/ulib/blobfs/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/bitmap/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/fs-management/include \
    -Isystem/ulib/minfs/include \

MODULE_HOST_LIBS := \
    third_party/ulib/uboringssl.hostlib \
    third_party/ulib/lz4.hostlib \
    system/uapp/blobfs.hostlib \
    system/ulib/fvm.hostlib \
    system/ulib/fbl.hostlib \
    system/ulib/digest.hostlib \
    system/ulib/minfs.hostlib \

MODULE_DEFINES += DISABLE_THREAD_ANNOTATIONS

include make/module.mk
