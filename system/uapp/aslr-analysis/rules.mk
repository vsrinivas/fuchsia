# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/main.cpp

MODULE_STATIC_LIBS := \
    ulib/runtime

MODULE_LIBS := \
    ulib/launchpad \
    ulib/magenta \
    ulib/c \
    ulib/mxcpp \
    ulib/mxio \
    ulib/mxtl \

include make/module.mk
