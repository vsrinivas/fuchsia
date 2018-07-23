# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).hostapp

MODULE_TYPE := hosttest

MODULE_NAME := fs-test

MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/util.cpp \
    $(LOCAL_DIR)/test-basic.cpp \
    $(LOCAL_DIR)/test-directory.cpp \
    $(LOCAL_DIR)/test-maxfile.cpp \
    $(LOCAL_DIR)/test-rw-workers.cpp \
    $(LOCAL_DIR)/test-sparse.cpp \
    $(LOCAL_DIR)/test-truncate.cpp \
    system/ulib/bitmap/raw-bitmap.cpp \
    system/ulib/fs/vfs.cpp \
    system/ulib/fs/vnode.cpp \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/unittest/include \
    -Isystem/ulib/bitmap/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/fs-management/include \
    -Isystem/ulib/fzl/include \
    -Isystem/ulib/minfs/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/zxcpp/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/zircon/include \

MODULE_HOST_LIBS := \
    system/ulib/unittest.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/minfs.hostlib \
    system/ulib/fbl.hostlib

# The VFS library uses Clang's thread annotations extensively, but
# the mutex implementation is not shared between target / host. As a
# consequence, host-side thread annotations are disabled so all
# thread annotation macros (referencing mutexes) are ignored.
MODULE_DEFINES += DISABLE_THREAD_ANNOTATIONS

include make/module.mk
