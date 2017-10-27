# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest
MODULE_USERTEST_GROUP := fs
MODULE_NAME := installer-test

MODULE_SRCS := $(LOCAL_DIR)/installer-tests.c

MODULE_STATIC_LIBS := \
	system/ulib/installer \
	system/ulib/gpt \
	third_party/ulib/cksum \

MODULE_LIBS := \
	system/ulib/unittest \
	system/ulib/fs-management \
	system/ulib/fdio \
	system/ulib/c \
	system/ulib/zircon \

include make/module.mk


MODULE := $(LOCAL_DIR).sparse

MODULE_TYPE := usertest
MODULE_USERTEST_GROUP := fs
MODULE_NAME := sparse-test

MODULE_SRCS := $(LOCAL_DIR)/sparse-tests.c

MODULE_STATIC_LIBS := \
	system/ulib/installer \

MODULE_LIBS := \
	system/ulib/unittest \
	system/ulib/fdio \
	system/ulib/c \
	system/ulib/zircon \

include make/module.mk

