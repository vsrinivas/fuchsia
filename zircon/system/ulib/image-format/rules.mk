# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# Userspace static library.
#

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib
MODULE_PACKAGE := src

MODULE_SRCS += \
    $(LOCAL_DIR)/image_format.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fidl \

MODULE_FIDL_LIBS := system/fidl/fuchsia-sysmem

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \

include make/module.mk
