# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := test

MODULE_SRCS += \
	$(LOCAL_DIR)/publish-data-helper.c

MODULE_NAME := publish-data-helper

# TODO: don't instrument the helper until we have a way to route sinks from the
# inner runtests instance to the outer one so they make it into summary.json.
MODULE_COMPILEFLAGS += $(NO_SANCOV) $(NO_PROFILE)

MODULE_LIBS := \
	system/ulib/unittest \
	system/ulib/fdio \
	system/ulib/zircon \
	system/ulib/c

include make/module.mk
