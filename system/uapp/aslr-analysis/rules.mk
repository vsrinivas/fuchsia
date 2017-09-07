# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/main.cpp

MODULE_STATIC_LIBS := \
    system/ulib/mxcpp \
    system/ulib/fbl \
    system/ulib/runtime

MODULE_LIBS := \
    system/ulib/launchpad \
    system/ulib/magenta \
    system/ulib/c \
    system/ulib/mxio \

include make/module.mk
