# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).host

MODULE_NAME := runtests

MODULE_TYPE := hostapp

MODULE_SRCS := \
    $(LOCAL_DIR)/runtests.cpp \

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/runtests-utils/include \

MODULE_HEADER_DEPS := \
    system/ulib/zircon-internal \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/runtests-utils.hostlib \

include make/module.mk
