# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

# devmgr - core userspace services process
#
MODULE_NAME := devmgr

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/dnode.c \
    $(LOCAL_DIR)/devmgr.c \
    $(LOCAL_DIR)/devmgr-mxio.c \
    $(LOCAL_DIR)/devmgr-rpc-server.c \
    $(LOCAL_DIR)/shared.c \
    $(LOCAL_DIR)/vfs-boot.c \
    $(LOCAL_DIR)/vfs.c \
    $(LOCAL_DIR)/vfs-device.c \
    $(LOCAL_DIR)/vfs-memory.c \
    $(LOCAL_DIR)/vfs-rpc.c

# userboot supports loading via the dynamic linker, so libc (ulib/musl)
# can be linked dynamically.  But it doesn't support any means to look
# up other shared libraries, so everything else must be linked statically.

# ddk is needed only for ddk/device.h
MODULE_STATIC_LIBS := \
    ulib/ddk \
    ulib/launchpad \
    ulib/elfload \
    ulib/mxio

MODULE_LIBS := ulib/magenta ulib/musl

MODULE_CFLAGS += -DDEVMGR=1

include make/module.mk


# devhost - container for drivers
#
# currently we compile in all the core drivers
# these will migrate to shared libraries soon
#
MODULE := $(LOCAL_DIR)-host

MODULE_NAME := devhost

MODULE_TYPE := userapp

LOCAL_SAVEDIR := $(LOCAL_DIR)
DRIVER_SRCS :=
DRIVERS := $(patsubst %/rules.mk,%,$(wildcard system/udev/*/driver.mk))
DRIVERS += $(patsubst %/rules.mk,%,$(wildcard third_party/udev/*/driver.mk))
-include $(DRIVERS)
LOCAL_DIR := $(LOCAL_SAVEDIR)

MODULE_DEFINES := MAGENTA_BUILTIN_DRIVERS=1

MODULE_SRCS := \
    $(LOCAL_DIR)/acpi.c \
    $(LOCAL_DIR)/acpi-device.c \
    $(LOCAL_DIR)/dmctl.c \
    $(LOCAL_DIR)/shared.c \
    $(LOCAL_DIR)/devhost-main.c \
    $(LOCAL_DIR)/devhost.c \
    $(LOCAL_DIR)/devhost-api.c \
    $(LOCAL_DIR)/devhost-binding.c \
    $(LOCAL_DIR)/devhost-core.c \
    $(LOCAL_DIR)/devhost-rpc-server.c \
    $(DRIVER_SRCS) \

# hexdump, hid, gfx are needed for various drivers
# TODO: remove when drivers are no longer linked in to devhost
MODULE_STATIC_LIBS := ulib/acpisvc-client ulib/ddk ulib/hid

MODULE_LIBS := ulib/driver ulib/mxio ulib/launchpad ulib/magenta ulib/musl

include make/module.mk

