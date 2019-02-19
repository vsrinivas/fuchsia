# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/profile.cpp

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-scheduler

MODULE_NAME := profile-svc-test

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \

MODULE_STATIC_LIBS := \
    system/ulib/profile

include make/module.mk
