# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/mxsh.c \
    $(LOCAL_DIR)/builtin.c \

MODULE_NAME := mxsh

MODULE_STATIC_LIBS := ulib/ddk ulib/hexdump

MODULE_LIBS := \
    ulib/mxio ulib/launchpad ulib/magenta ulib/musl

USER_MANIFEST_LINES += docs/LICENSE=kernel/LICENSE

ifneq ($(wildcard autorun),)
USER_MANIFEST_LINES += autorun=autorun
endif

include make/module.mk
