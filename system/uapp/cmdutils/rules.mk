# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).env

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += $(LOCAL_DIR)/env.c

MODULE_NAME := env

MODULE_LIBS := system/ulib/zircon system/ulib/c system/ulib/launchpad

include make/module.mk
