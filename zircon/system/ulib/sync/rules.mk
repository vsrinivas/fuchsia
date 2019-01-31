# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS += \
    $(LOCAL_DIR)/completion.c \
    $(LOCAL_DIR)/condition.cpp \
    $(LOCAL_DIR)/mutex.c \

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/zircon-internal

MODULE_EXPORT := a

# This code is used in early startup, where safe-stack setup is not ready yet.
MODULE_COMPILEFLAGS += $(NO_SAFESTACK) $(NO_SANITIZERS)

include make/module.mk
