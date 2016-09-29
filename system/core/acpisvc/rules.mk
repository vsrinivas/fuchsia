# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := acpisvc

MODULE_TYPE := userapp

MODULE_CFLAGS += -Wno-strict-aliasing -Ithird_party/lib/acpica/source/include

ifeq ($(ARCH),x86)
MODULE_SRCS += \
    $(LOCAL_DIR)/debug.c \
    $(LOCAL_DIR)/ec.c \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/pci.c \
    $(LOCAL_DIR)/power.c \
    $(LOCAL_DIR)/powerbtn.c \
    $(LOCAL_DIR)/processor.c
else
MODULE_SRCS += $(LOCAL_DIR)/dummy.c
endif

MODULE_STATIC_LIBS := \
    ulib/acpisvc-client \
    ulib/acpica \
    ulib/ddk \

MODULE_LIBS := \
    ulib/magenta \
    ulib/musl \
    ulib/mxio \

include make/module.mk
