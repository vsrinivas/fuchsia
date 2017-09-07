# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/ethertap.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/mx \
    system/ulib/mxcpp \
    system/ulib/fbl \
    system/ulib/pretty \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/magenta \
    system/ulib/c

include make/module.mk
