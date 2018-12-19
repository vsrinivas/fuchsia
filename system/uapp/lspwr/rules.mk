# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += $(LOCAL_DIR)/lspwr.cpp

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zxcpp

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-power

include make/module.mk
