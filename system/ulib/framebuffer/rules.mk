# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)
MODULE_NAME := framebuffer

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/framebuffer.c

MODULE_STATIC_LIBS := system/ulib/fidl

MODULE_LIBS := system/ulib/zircon system/ulib/fdio system/ulib/c

MODULE_FIDL_LIBS := system/fidl/fuchsia-display

MODULE_PACKAGE := static

include make/module.mk
