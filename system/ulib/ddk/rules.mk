# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_DEFINES := LIBDDK=1

MODULE_SRCS += \
    $(LOCAL_DIR)/common/hid.c \
    $(LOCAL_DIR)/common/usb.c \
    $(LOCAL_DIR)/completion.c \
    $(LOCAL_DIR)/protocol/input.c \
    $(LOCAL_DIR)/io-alloc.c \
    $(LOCAL_DIR)/iotxn.c \

ifeq ($(ARCH),arm)
MODULE_SRCS += system/ulib/magenta/syscalls-arm32.S
else ifeq ($(ARCH),arm64)
MODULE_SRCS += system/ulib/magenta/syscalls-arm64.S
else ifeq ($(ARCH),x86)
    ifeq ($(SUBARCH),x86-64)
    MODULE_SRCS += system/ulib/magenta/syscalls-x86-64.S
    else
    MODULE_SRCS += system/ulib/magenta/syscalls-x86.S
    endif
endif

MODULE_DEPS += \
    ulib/musl \
    ulib/magenta

MODULE_STATIC_LIBS := ulib/hexdump

MODULE_EXPORT := ddk

include make/module.mk
