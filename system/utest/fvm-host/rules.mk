# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostapp

MODULE_SRCS += \
    $(LOCAL_DIR)/main.cpp \
    system/uapp/fvm/container/container.cpp \
    system/uapp/fvm/container/fvm.cpp \
    system/uapp/fvm/container/sparse.cpp \
    system/uapp/fvm/format/format.cpp \
    system/uapp/fvm/format/minfs.cpp \
    system/uapp/fvm/format/blobstore.cpp \
    system/uapp/blobstore/blobstore-common.cpp \

MODULE_NAME := fvm-host-test

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/uapp/fvm/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fvm/include \
    -Isystem/ulib/digest/include \
    -Ithird_party/ulib/cryptolib/include \
    -Isystem/ulib/gpt/include \
    -Isystem/uapp/blobstore/include \
    -Isystem/ulib/bitmap/include \
    -Isystem/ulib/fs-management/include \
    -Isystem/ulib/minfs/include \
    -Isystem/ulib/unittest/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/fdio/include \

MODULE_HOST_LIBS := \
    system/uapp/fvm.hostlib \
    system/ulib/unittest.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/minfs.hostlib \
    system/ulib/fbl.hostlib \
    system/uapp/blobstore.hostlib \

MODULE_DEFINES += DISABLE_THREAD_ANNOTATIONS

include make/module.mk
