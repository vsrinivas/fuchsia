# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)


MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_DIR)/kvstore.c

MODULE_STATIC_LIBS := third_party/ulib/cksum

MODULE_LIBS := system/ulib/c

include make/module.mk


MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_NAME := kvstore-test

MODULE_SRCS := $(LOCAL_DIR)/kvstore-test.c

MODULE_STATIC_LIBS := system/ulib/kvstore system/ulib/pretty third_party/ulib/cksum

MODULE_LIBS := system/ulib/unittest system/ulib/fdio system/ulib/c

include make/module.mk
