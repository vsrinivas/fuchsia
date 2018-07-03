# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/launcher.cpp \
    $(LOCAL_DIR)/provider.cpp

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-process

MODULE_HEADER_DEPS := \
    system/ulib/svc \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zxcpp \
    system/ulib/zx

MODULE_LIBS := \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk
