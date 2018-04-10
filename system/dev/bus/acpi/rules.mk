# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ifeq ($(ARCH),x86)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_NAME := bus-acpi

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
MODULE_CFLAGS += -Wno-null-pointer-arithmetic
endif
MODULE_CFLAGS += -fno-strict-aliasing

MODULE_COMPILEFLAGS += -Ithird_party/lib/acpica/source/include

MODULE_SRCS := \
    $(LOCAL_DIR)/bus-acpi.c \
    $(LOCAL_DIR)/cpu-trace.c \
    $(LOCAL_DIR)/dev-battery.c \
    $(LOCAL_DIR)/dev-cros-ec/dev.cpp \
    $(LOCAL_DIR)/dev-cros-ec/motion.cpp \
    $(LOCAL_DIR)/dev-ec.c \
    $(LOCAL_DIR)/dev-pwrsrc.c \
    $(LOCAL_DIR)/dev-tbmc.cpp \
    $(LOCAL_DIR)/dev-thermal.c \
    $(LOCAL_DIR)/debug.c \
    $(LOCAL_DIR)/init.c \
    $(LOCAL_DIR)/pci.c \
    $(LOCAL_DIR)/power.c \
    $(LOCAL_DIR)/powerbtn.c \
    $(LOCAL_DIR)/resources.c \
    $(LOCAL_DIR)/util.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/hid \
    third_party/ulib/acpica \
    third_party/ulib/chromiumos-platform-ec \
    system/ulib/zxcpp \
    system/ulib/fbl \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \

else # !ARCH=x86

MODULE_NAME := bus-acpi

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += $(LOCAL_DIR)/dummy.c

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \

endif # ARCH=x86

include make/module.mk
