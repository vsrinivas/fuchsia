# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core
MODULE_NAME := nand-util

MODULE_SRCS := \
    $(LOCAL_DIR)/aml.cpp \
    $(LOCAL_DIR)/main.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/pretty \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/cksum \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-nand \
    system/fidl/zircon-nand \

include make/module.mk
