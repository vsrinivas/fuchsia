# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/sanitizer-utils.c

MODULE_NAME := sanitizer-utils-test
MODULE_USERTEST_GROUP := libc

MODULE_STATIC_LIBS := \
    system/ulib/loader-service \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/ldmsg \

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/launchpad \
    system/ulib/async.default \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c

include make/module.mk
