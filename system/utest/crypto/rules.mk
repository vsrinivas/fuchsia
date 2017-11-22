# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/aead.cpp \
    $(LOCAL_DIR)/bytes.cpp \
    $(LOCAL_DIR)/cipher.cpp \
    $(LOCAL_DIR)/hkdf.cpp \
    $(LOCAL_DIR)/hmac.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/utils.cpp \

MODULE_NAME := crypto-test

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/crypto \
    system/ulib/unittest \

MODULE_STATIC_LIBS := \
    system/ulib/zxcpp \
    system/ulib/fbl \

include make/module.mk
