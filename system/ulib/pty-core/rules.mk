# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += $(LOCAL_DIR)/pty-core.c $(LOCAL_DIR)/pty-fifo.c

MODULE_HEADER_DEPS := ulib/ddk

MODULE_LIBS := ulib/magenta ulib/c

include make/module.mk
