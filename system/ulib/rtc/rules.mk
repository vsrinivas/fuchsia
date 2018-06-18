# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := rtc

MODULE_TYPE := userlib

MODULE_EXPORT := so
MODULE_SO_NAME := rtc

MODULE_COMPILEFLAGS := -fvisibility=hidden

MODULE_SRCS := \
	$(LOCAL_DIR)/librtc.c

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
