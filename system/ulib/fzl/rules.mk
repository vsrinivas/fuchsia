# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/mapped-vmo.cpp \
    $(LOCAL_DIR)/time.cpp \

MODULE_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \

MODULE_PACKAGE := src

include make/module.mk
