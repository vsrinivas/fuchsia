# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := cat

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/cat.c

MODULE_NAME := cat

MODULE_LIBS := ulib/mxio ulib/magenta ulib/musl

include make/module.mk

MODULE := true

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/true.c

MODULE_NAME := true

MODULE_LIBS := ulib/magenta ulib/musl

include make/module.mk

MODULE := false

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/false.c

MODULE_NAME := false

MODULE_LIBS := ulib/magenta ulib/musl

include make/module.mk

MODULE := env

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/env.c

MODULE_NAME := env

MODULE_LIBS := ulib/magenta ulib/musl ulib/launchpad

include make/module.mk
