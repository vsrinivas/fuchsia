# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# disabled for now
ifeq (1,2)

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/bad-kernel-access.c

MODULE_NAME := bad-kernel-access-test-crashes

MODULE_STATIC_LIBS := system/ulib/ddk
MODULE_LIBS := system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk

endif
