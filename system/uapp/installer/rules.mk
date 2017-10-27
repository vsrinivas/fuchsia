# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userapp

MODULE_NAME := install-fuchsia

MODULE_GROUP := core

MODULE_SRCS := $(LOCAL_DIR)/install-fuchsia.c

MODULE_STATIC_LIBS := \
	system/ulib/gpt \
	third_party/ulib/lz4 \
	third_party/ulib/cksum \
	system/ulib/installer

MODULE_LIBS := \
	system/ulib/fs-management \
	system/ulib/fdio \
	system/ulib/zircon \
	system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).sparse

MODULE_NAME := sparse

MODULE_TYPE := hostapp

MODULE_SRCS := $(LOCAL_DIR)/sparse.c system/ulib/installer/sparse.c

MODULE_COMPILEFLAGS := -Isystem/ulib/installer/include

include make/module.mk