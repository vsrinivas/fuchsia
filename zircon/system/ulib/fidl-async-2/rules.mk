# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# Userspace library.
#

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib
MODULE_PACKAGE := src

# MODULE_SRCS is empty because this lib is header-only for now.
#MODULE_SRCS += \
#    $(LOCAL_DIR)/fidl_server.cpp \
#    $(LOCAL_DIR)/simple_binding.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/fit \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \

include make/module.mk
