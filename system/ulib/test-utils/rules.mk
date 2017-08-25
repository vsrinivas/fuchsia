# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_GROUP := test

MODULE_SRCS += \
    $(LOCAL_DIR)/test-utils.c

MODULE_SO_NAME := test-utils

# elfload, runtime are static as they currently aren't built as sos.
MODULE_STATIC_LIBS := \
    system/ulib/elfload \
    system/ulib/runtime

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/launchpad \
    system/ulib/mxio \
    system/ulib/c \
    system/ulib/magenta

include make/module.mk
