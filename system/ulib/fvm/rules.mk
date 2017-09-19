# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/fvm.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/digest \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/gpt \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/cryptolib \

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \

include make/module.mk


MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS := \
    $(LOCAL_DIR)/fvm.cpp \
    system/ulib/digest/digest.cpp \
    system/ulib/digest/merkle-tree.cpp \
    third_party/ulib/cryptolib/cryptolib.c \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/zircon/include \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/digest/include \
    -Ithird_party/ulib/cryptolib/include \
    -Isystem/ulib/gpt/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/block-client/include \
    -Isystem/ulib/fs/include \
    -Isystem/ulib/zx/include \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib

MODULE_DEFINES += DISABLE_THREAD_ANNOTATIONS

include make/module.mk
