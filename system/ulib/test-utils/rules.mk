# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/test-utils.c

MODULE_SO_NAME := test-utils

# launchpad, elfload, mxio are static so that every unittest doesn't have to
# mention them as a dependency as well.
# N.B. The order is important. Think ordering of args to the linker.
MODULE_STATIC_LIBS := \
    ulib/launchpad \
    ulib/elfload \
    ulib/mxio \
    ulib/runtime
MODULE_LIBS := \
    ulib/unittest \
    ulib/musl \
    ulib/magenta

include make/module.mk
