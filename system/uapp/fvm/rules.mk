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
    $(LOCAL_DIR)/format/blobstore.cpp \
    $(LOCAL_DIR)/format/format.cpp \
    $(LOCAL_DIR)/format/minfs.cpp \
    system/ulib/fs/vfs.cpp \
    system/ulib/fs/vnode.cpp \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/uapp/fvm/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/digest/include \
    -Ithird_party/ulib/cryptolib/include \
    -Isystem/ulib/gpt/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/fvm/include \
    -Isystem/ulib/minfs/include \
    -Isystem/ulib/bitmap/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/zircon/include \
    -Isystem/ulib/fs-management/include \
    -Isystem/ulib/blobstore/include \
    -Isystem/ulib/fbl/include \

MODULE_HOST_LIBS := \
    system/ulib/blobstore.hostlib \
    system/ulib/fvm.hostlib \
    system/ulib/fbl.hostlib \
    system/ulib/minfs.hostlib \

MODULE_DEFINES += DISABLE_THREAD_ANNOTATIONS

include make/module.mk
