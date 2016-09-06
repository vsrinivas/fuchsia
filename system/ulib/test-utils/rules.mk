# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/test-utils.c

MODULE_SO_NAME := test-utils

# elfload, runtime are static as they currently aren't built as sos.
MODULE_STATIC_LIBS := \
    ulib/elfload \
    ulib/runtime

MODULE_LIBS := \
    ulib/unittest \
    ulib/launchpad \
    ulib/mxio \
    ulib/musl \
    ulib/magenta

include make/module.mk
