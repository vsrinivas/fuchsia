# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

ENABLE_DEVHOST_V2 := $(call TOBOOL,$(ENABLE_DEVHOST_V2))

MODULE := $(LOCAL_DIR)

# devmgr - core userspace services process
#
MODULE_NAME := devmgr

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/dnode.cpp \
    $(LOCAL_DIR)/devmgr.c \
    $(LOCAL_DIR)/devmgr-coordinator.c \
    $(LOCAL_DIR)/devmgr-mxio.c \
    $(LOCAL_DIR)/shared.c \
    $(LOCAL_DIR)/vfs-boot.cpp \
    $(LOCAL_DIR)/vfs-device.cpp \
    $(LOCAL_DIR)/vfs-memory.cpp \
    $(LOCAL_DIR)/vfs-rpc.cpp

# userboot supports loading via the dynamic linker, so libc (system/ulib/c)
# can be linked dynamically.  But it doesn't support any means to look
# up other shared libraries, so everything else must be linked statically.

# ddk is needed only for ddk/device.h
MODULE_HEADER_DEPS := \
	system/ulib/ddk

MODULE_STATIC_LIBS := \
    system/ulib/gpt \
    system/ulib/launchpad \
    system/ulib/elfload \
    system/ulib/mxcpp \
    system/ulib/mxio \
    system/ulib/mxtl \
    system/ulib/fs \
    system/ulib/fs-management \
    system/ulib/bootdata \
    third_party/ulib/lz4

MODULE_LIBS := system/ulib/magenta system/ulib/c

MODULE_DEFINES := DEVMGR=1

ifeq ($(ENABLE_DEVHOST_V2),true)
MODULE_SRCS += \
    $(LOCAL_DIR)/devmgr-coordinator-v2.c \
    $(LOCAL_DIR)/devmgr-drivers.c \
    $(LOCAL_DIR)/devhost-binding.c \
    $(LOCAL_DIR)/devhost-shared.c \
    $(LOCAL_DIR)/driver-info.c \
    $(LOCAL_DIR)/acpi.c

MODULE_DEFINES += DEVHOST_V2=1

MODULE_STATIC_LIBS += system/ulib/acpisvc-client
endif


include make/module.mk


# devhost - container for drivers
#
# currently we compile in all the core drivers
# these will migrate to shared libraries soon
#
MODULE := $(LOCAL_DIR).host

MODULE_NAME := devhost

MODULE_TYPE := userapp

LOCAL_SAVEDIR := $(LOCAL_DIR)
DRIVER_SRCS :=
DRIVERS := $(patsubst %/rules.mk,%,$(wildcard system/udev/*/driver.mk))
-include $(DRIVERS)
LOCAL_DIR := $(LOCAL_SAVEDIR)

MODULE_DEFINES := MAGENTA_BUILTIN_DRIVERS=1

MODULE_SRCS := \
    $(LOCAL_DIR)/acpi.c \
    $(LOCAL_DIR)/acpi-device.c \
    $(LOCAL_DIR)/dmctl.c \
    $(LOCAL_DIR)/shared.c \
    $(LOCAL_DIR)/devhost.c \
    $(LOCAL_DIR)/devhost-api.c \
    $(LOCAL_DIR)/devhost-binding.c \
    $(LOCAL_DIR)/devhost-drivers.c \
    $(LOCAL_DIR)/devhost-core.c \
    $(LOCAL_DIR)/devhost-rpc-server.c \
    $(LOCAL_DIR)/driver-info.c \
    $(DRIVER_SRCS) \

MODULE_STATIC_LIBS := system/ulib/acpisvc-client system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/mxio system/ulib/launchpad system/ulib/magenta system/ulib/c

include make/module.mk


# devhost - container for drivers - v2
#
MODULE := $(LOCAL_DIR).host2

MODULE_NAME := devhost2

MODULE_TYPE := userapp

MODULE_DEFINES := MAGENTA_BUILTIN_DRIVERS=1

MODULE_DEFINES += DEVHOST_V2=1
MODULE_SRCS := \
    $(LOCAL_DIR)/devhost-api.c \
    $(LOCAL_DIR)/devhost-core.c \
    $(LOCAL_DIR)/devhost-v2.c \
    $(LOCAL_DIR)/devhost-shared.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk

