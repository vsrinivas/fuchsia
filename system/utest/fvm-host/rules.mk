# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hosttest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.cpp \
    system/host/fvm/container/container.cpp \
    system/host/fvm/container/fvm.cpp \
    system/host/fvm/container/sparse.cpp \
    system/host/fvm/format/format.cpp \
    system/host/fvm/format/minfs.cpp \
    system/host/fvm/format/blobfs.cpp \

MODULE_NAME := fvm-test

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Ithird_party/ulib/lz4/include \
    -Isystem/uapp/lz4/include \
    -Isystem/host/fvm/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fvm/include \
    -Isystem/ulib/digest/include \
    -Ithird_party/ulib/uboringssl/include \
    -Isystem/ulib/gpt/include \
    -Isystem/ulib/blobfs/include \
    -Isystem/ulib/bitmap/include \
    -Isystem/ulib/fs-management/include \
    -Isystem/ulib/minfs/include \
    -Isystem/ulib/unittest/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/fzl/include \
    -Isystem/ulib/fdio/include \

MODULE_HOST_LIBS := \
    third_party/ulib/uboringssl.hostlib \
    third_party/ulib/lz4.hostlib \
    system/ulib/fvm.hostlib \
    system/ulib/unittest.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/minfs.hostlib \
    system/ulib/fbl.hostlib \
    system/ulib/digest.hostlib \
    system/uapp/blobfs.hostlib \

MODULE_DEFINES += DISABLE_THREAD_ANNOTATIONS

include make/module.mk
