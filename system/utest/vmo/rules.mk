# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/bench.cpp \
    $(LOCAL_DIR)/vmo.cpp

MODULE_NAME := vmo-test

MODULE_STATIC_LIBS := system/ulib/pretty system/ulib/fbl system/ulib/zx
MODULE_LIBS := system/ulib/unittest system/ulib/fdio system/ulib/zircon system/ulib/c

include make/module.mk
