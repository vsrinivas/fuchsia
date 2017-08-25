# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += $(LOCAL_DIR)/iotime.c

MODULE_STATIC_LIBS := \
    system/ulib/block-client \
    system/ulib/sync

MODULE_LIBS := \
    system/ulib/fs-management \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

include make/module.mk
