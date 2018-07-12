# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/fsck.cpp \
    $(LOCAL_DIR)/fvm.cpp \
    $(LOCAL_DIR)/launch.cpp \
    $(LOCAL_DIR)/mkfs.cpp \
    $(LOCAL_DIR)/mount.cpp \
    $(LOCAL_DIR)/ram-nand.cpp \
    $(LOCAL_DIR)/ramdisk.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fvm \
    system/ulib/fs \
    system/ulib/digest \
    system/ulib/ddk \
    system/ulib/gpt \
    system/ulib/fs \
    system/ulib/fzl \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/uboringssl \

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \

MODULE_EXPORT := so
MODULE_SO_NAME := fs-management

include make/module.mk
