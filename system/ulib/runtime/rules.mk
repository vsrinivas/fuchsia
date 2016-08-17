# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/message.c \
    $(LOCAL_DIR)/mutex.c \
    $(LOCAL_DIR)/once.c \
    $(LOCAL_DIR)/processargs.c \
    $(LOCAL_DIR)/process.c \
    $(LOCAL_DIR)/strstatus.c \
    $(LOCAL_DIR)/thread.c \
    $(LOCAL_DIR)/tls.c \
    $(LOCAL_DIR)/sysinfo.c \

MODULE_LIBS += \
    ulib/magenta

# for stdint.h
MODULE_HEADER_DEPS += \
    ulib/musl

MODULE_EXPORT := runtime

include make/module.mk
