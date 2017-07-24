# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

# This whole module is a hack to deal with these functions needing to be compiled with
# -fno-stack-protector
MODULE_SRCS += $(LOCAL_DIR)/threads.c
MODULE_COMPILEFLAGS += $(NO_SAFESTACK) $(NO_SANITIZERS)

MODULE_NAME := threads-test-threads

MODULE_LIBS := \
    system/ulib/unittest system/ulib/mxio system/ulib/magenta system/ulib/c
MODULE_STATIC_LIBS := system/ulib/runtime

include make/module.mk
