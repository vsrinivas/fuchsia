# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/jitterentropy-base.c

KERNEL_INCLUDES += $(LOCAL_DIR)/include

# According to its documentation, jitterentropy must be compiled at optimization
# level -O0.
#
# TODO(SEC-14): Test the code generated at various optimization levels. If there
# is a significant difference in entropy quality, replace the relevant C code by
# assembly code to protect against future compiler changes.
#
# The original Makefile also specifies -fwrapv.
#
# Several flags related to stack-protection were removed, for compiler
# compatibility.
MODULE_CFLAGS += -O0 -fwrapv

include make/module.mk
