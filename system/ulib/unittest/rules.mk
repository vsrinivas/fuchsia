# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_GROUP := test

MODULE_SRCS += \
    $(LOCAL_DIR)/all-tests.c \
    $(LOCAL_DIR)/crash-handler.c \
    $(LOCAL_DIR)/crash-list.c \
    $(LOCAL_DIR)/unittest.c \

MODULE_SO_NAME := unittest

# N.B. mxio, and thus launchpad, cannot appear here. See ./README.md.
MODULE_LIBS := system/ulib/c system/ulib/magenta

MODULE_STATIC_LIBS := system/ulib/pretty

include make/module.mk

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS += \
    $(LOCAL_DIR)/all-tests.c \
    $(LOCAL_DIR)/unittest.c \

MODULE_HOST_LIBS := system/ulib/pretty

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fbl/include

include make/module.mk
