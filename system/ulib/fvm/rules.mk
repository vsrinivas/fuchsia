# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/fvm.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fs \
    system/ulib/gpt \
    system/ulib/digest \
    system/ulib/mxcpp \
    system/ulib/fbl \
    third_party/ulib/cryptolib \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \

include make/module.mk
