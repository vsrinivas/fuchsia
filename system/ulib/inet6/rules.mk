# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/inet6.c \
    $(LOCAL_DIR)/netifc.c \

MODULE_LIBS += ulib/mxio ulib/magenta ulib/musl

include make/module.mk
