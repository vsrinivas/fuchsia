# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostapp

MODULE_SRCS += $(LOCAL_DIR)/bootserver.c
MODULE_SRCS += $(LOCAL_DIR)/netboot.c
MODULE_SRCS += $(LOCAL_DIR)/tftp.c

MODULE_HOST_LIBS := system/ulib/tftp

include make/module.mk
