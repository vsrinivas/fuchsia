# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/thread-injection.c

MODULE_NAME := thread-injection-test

MODULE_LIBS := \
    system/ulib/unittest system/ulib/launchpad system/ulib/zircon system/ulib/c

MODULES += $(LOCAL_DIR)/injector $(LOCAL_DIR)/injected

include make/module.mk
