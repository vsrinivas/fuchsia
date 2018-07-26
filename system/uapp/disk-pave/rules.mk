# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_NAME := install-disk-image

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/device-partitioner.cpp \
    $(LOCAL_DIR)/pave-lib.cpp \
    $(LOCAL_DIR)/disk-pave.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/gpt \
    system/ulib/block-client \
    system/ulib/chromeos-disk-setup \
    system/ulib/fs \
    system/ulib/fs-management \
    system/ulib/fvm \
    system/ulib/fzl \
    system/ulib/ddk \
    system/ulib/zx \
    system/ulib/fbl \
    system/ulib/digest \
    system/ulib/sync \
    system/ulib/zxcpp \
    third_party/ulib/cksum \
    third_party/ulib/uboringssl \
    third_party/ulib/lz4

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/crypto \
    system/ulib/zxcrypt \

MODULE_PACKAGE := src

include make/module.mk

