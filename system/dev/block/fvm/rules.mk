# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/fvm.c \
    $(LOCAL_DIR)/fvm.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fs \
    system/ulib/fvm \
    system/ulib/gpt \
    system/ulib/digest \
    system/ulib/mxcpp \
    system/ulib/fbl \
    system/ulib/sync \
    third_party/ulib/cryptolib \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/driver \
    system/ulib/magenta \
    system/ulib/mxio \

include make/module.mk
