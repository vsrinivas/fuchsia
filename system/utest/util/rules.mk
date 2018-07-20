# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
LOCAL_SRCS = \
	$(LOCAL_DIR)/listnode.cpp

# User test

MODULE := $(LOCAL_DIR)
MODULE_TYPE := usertest
MODULE_NAME := listnode-test
MODULE_SRCS := $(LOCAL_SRCS)
MODULE_LIBS := \
	system/ulib/c \
	system/ulib/unittest \
	system/ulib/zircon

include make/module.mk

# Host test

MODULE := $(LOCAL_DIR).hostapp
MODULE_TYPE := hosttest
MODULE_NAME := listnode-test
MODULE_SRCS := $(LOCAL_SRCS)
MODULE_COMPILEFLAGS := \
    -Isystem/ulib/unittest/include
MODULE_HOST_LIBS += \
	system/ulib/pretty.hostlib \
    system/ulib/unittest.hostlib \

include make/module.mk
