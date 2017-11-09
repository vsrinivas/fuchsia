# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/digest.cpp \
    $(LOCAL_DIR)/merkle-tree.cpp

MODULE_SO_NAME := digest
MODULE_LIBS := system/ulib/c

MODULE_STATIC_LIBS := \
    third_party/ulib/uboringssl \
    system/ulib/zxcpp \
    system/ulib/fbl \

include make/module.mk


MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_COMPILEFLAGS := \
    -Ithird_party/ulib/uboringssl/include \
    -Isystem/ulib/fbl/include \

MODULE_SRCS += \
    $(LOCAL_DIR)/digest.cpp \
    $(LOCAL_DIR)/merkle-tree.cpp

MODULE_HOST_LIBS := \
    third_party/ulib/uboringssl.hostlib \
    system/ulib/fbl.hostlib \

MODULE_DEFINES += DISABLE_THREAD_ANNOTATIONS

include make/module.mk
