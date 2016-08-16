# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := devmgr

MODULE_TYPE := userapp

# grab sources for all built-in drivers
# someday these will become dynamically loadable modules
LOCAL_SAVEDIR := $(LOCAL_DIR)
DRIVER_SRCS :=
DRIVERS := $(patsubst %/rules.mk,%,$(wildcard system/udev/*/driver.mk))
DRIVERS += $(patsubst %/rules.mk,%,$(wildcard third_party/udev/*/driver.mk))
-include $(DRIVERS)
LOCAL_DIR := $(LOCAL_SAVEDIR)

MODULE_SRCS := \
    $(DRIVER_SRCS) \

MODULE_SRCS += \
    $(LOCAL_DIR)/devmgr.c \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/rpc-device.c \
    $(LOCAL_DIR)/rpc-devhost.c \
    $(LOCAL_DIR)/devhost.c \
    $(LOCAL_DIR)/dmctl.c \
    $(LOCAL_DIR)/api.c \
    $(LOCAL_DIR)/vfs.c \
    $(LOCAL_DIR)/vfs-device.c \
    $(LOCAL_DIR)/vfs-boot.c \
    $(LOCAL_DIR)/vfs-memory.c \
    $(LOCAL_DIR)/dnode.c \
    $(LOCAL_DIR)/mxio.c \
    $(LOCAL_DIR)/main.c

# userboot supports loading via the dynamic linker, so libc (ulib/musl)
# can be linked dynamically.  But it doesn't support any means to look
# up other shared libraries, so everything else must be linked statically.

MODULE_STATIC_LIBS := \
    ulib/ddk \
    ulib/hid \
    ulib/launchpad \
    ulib/elfload \
    ulib/mxio \
    ulib/gfx \
    ulib/runtime \
    ulib/magenta

MODULE_LIBS := ulib/musl

include make/module.mk
