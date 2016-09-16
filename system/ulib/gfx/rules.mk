# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO fix to share this lib in user/kernel

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_LIBS := \
    ulib/magenta \
    ulib/musl \

MODULE_SRCS += \
    $(LOCAL_DIR)/gfx.c \

include make/module.mk
