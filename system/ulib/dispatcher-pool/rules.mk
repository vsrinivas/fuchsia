# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/dispatcher-channel.cpp \
    $(LOCAL_DIR)/dispatcher-event-source.cpp \
    $(LOCAL_DIR)/dispatcher-execution-domain.cpp \
    $(LOCAL_DIR)/dispatcher-thread-pool.cpp \
    $(LOCAL_DIR)/dispatcher-timer.cpp \
    $(LOCAL_DIR)/dispatcher-wakeup-event.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \

MODULE_PACKAGE := src

include make/module.mk
