# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/test-device.cpp \
    $(LOCAL_DIR)/volume.cpp \
    $(LOCAL_DIR)/zxcrypt.cpp \


MODULE_NAME := zxcrypt-test

MODULE_COMPILEFLAGS += -Isystem/utest

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/crypto \
    system/ulib/digest \
    system/ulib/fs-management \
    system/ulib/unittest \
    system/ulib/zxcrypt \

MODULE_STATIC_LIBS := \
    third_party/ulib/cryptolib \
    third_party/ulib/uboringssl \
    system/ulib/block-client \
    system/ulib/ddk \
    system/ulib/fvm \
    system/ulib/fs \
    system/ulib/gpt \
    system/ulib/pretty \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \

include make/module.mk
