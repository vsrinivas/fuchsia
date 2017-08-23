# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ifeq ($(ARCH),x86)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_NAME := bus-acpi

MODULE_CFLAGS += -fno-strict-aliasing -Ithird_party/lib/acpica/source/include

MODULE_SRCS := \
    $(LOCAL_DIR)/bus-acpi.c \
    $(LOCAL_DIR)/dev-battery.c \
    $(LOCAL_DIR)/dev-ec.c \
    $(LOCAL_DIR)/dev-pwrsrc.c \
    $(LOCAL_DIR)/debug.c \
    $(LOCAL_DIR)/init.c \
    $(LOCAL_DIR)/pci.c \
    $(LOCAL_DIR)/power.c \
    $(LOCAL_DIR)/powerbtn.c \
    $(LOCAL_DIR)/processor.c \
    $(LOCAL_DIR)/resources.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/acpisvc-client \
    third_party/ulib/acpica \
    system/ulib/mxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/magenta \
    system/ulib/c \
    system/ulib/mxio \

else # !ARCH=x86

MODULE_NAME := acpisvc

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/dummy.c

MODULE_LIBS := \
    system/ulib/magenta \
    system/ulib/c \
    system/ulib/mxio \

endif # ARCH=x86

include make/module.mk
