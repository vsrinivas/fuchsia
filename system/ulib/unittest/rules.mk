# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_GROUP := test

MODULE_SRCS += \
    $(LOCAL_DIR)/all-tests.cpp \
    $(LOCAL_DIR)/crash-handler.cpp \
    $(LOCAL_DIR)/crash-list.cpp \
    $(LOCAL_DIR)/unittest.cpp \

MODULE_SO_NAME := unittest

# N.B. fdio, and thus launchpad, cannot appear here. See ./README.md.
MODULE_LIBS := system/ulib/c system/ulib/zircon

MODULE_STATIC_LIBS := system/ulib/pretty

MODULE_PACKAGE := src

include make/module.mk

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS += \
    $(LOCAL_DIR)/all-tests.cpp \
    $(LOCAL_DIR)/unittest.cpp \

MODULE_HOST_LIBS := system/ulib/pretty

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fbl/include

include make/module.mk
