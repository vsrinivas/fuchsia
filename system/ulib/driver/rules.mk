# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := system/core/devmgr

MODULE := system/ulib/driver

MODULE_NAME := driver

MODULE_TYPE := userlib

MODULE_SO_NAME := driver

MODULE_COMPILEFLAGS := -fvisibility=hidden

MODULE_SRCS := $(LOCAL_DIR)/driver-api.c

MODULE_STATIC_LIBS := ulib/ddk

#MODULE_LIBS := ulib/mxio ulib/launchpad ulib/magenta ulib/musl

include make/module.mk
