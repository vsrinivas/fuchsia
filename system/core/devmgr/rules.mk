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
    $(LOCAL_DIR)/acpi.c \
    $(LOCAL_DIR)/dnode.cpp \
    $(LOCAL_DIR)/devhost-shared.c \
    $(LOCAL_DIR)/devmgr.c \
    $(LOCAL_DIR)/devmgr-binding.c \
    $(LOCAL_DIR)/devmgr-coordinator.c \
    $(LOCAL_DIR)/devmgr-devfs.c \
    $(LOCAL_DIR)/devmgr-drivers.c \
    $(LOCAL_DIR)/devmgr-mxio.c \
    $(LOCAL_DIR)/driver-info.c \
    $(LOCAL_DIR)/vfs-boot.cpp \
    $(LOCAL_DIR)/vfs-memory.cpp \
    $(LOCAL_DIR)/vfs-rpc.cpp \

# userboot supports loading via the dynamic linker, so libc (system/ulib/c)
# can be linked dynamically.  But it doesn't support any means to look
# up other shared libraries, so everything else must be linked statically.

# ddk is needed only for ddk/device.h
MODULE_HEADER_DEPS := \
	system/ulib/ddk

MODULE_STATIC_LIBS := \
    system/ulib/gpt \
    system/ulib/fs \
    system/ulib/bootdata \
    third_party/ulib/lz4 \
    system/ulib/mxalloc \
    system/ulib/mxcpp \
    system/ulib/mxtl \
    system/ulib/acpisvc-client \

MODULE_LIBS := \
    system/ulib/fs-management \
    system/ulib/launchpad \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

MODULE_DEFINES := DDK_INTERNAL=1

include make/module.mk


# devhost - container for drivers
#
MODULE := $(LOCAL_DIR).host

MODULE_NAME := devhost

MODULE_TYPE := userapp

MODULE_DEFINES := MAGENTA_BUILTIN_DRIVERS=1 DDK_INTERNAL=1

MODULE_SRCS := \
	$(LOCAL_DIR)/devhost.c \
    $(LOCAL_DIR)/devhost-api.c \
    $(LOCAL_DIR)/devhost-core.c \
    $(LOCAL_DIR)/devhost-rpc-server.c \
    $(LOCAL_DIR)/devhost-shared.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk



MODULE := $(LOCAL_DIR).dmctl

MODULE_TYPE := driver

MODULE_NAME := dmctl

MODULE_SRCS := \
	$(LOCAL_DIR)/dmctl.c \
	$(LOCAL_DIR)/devhost-shared.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/mxio system/ulib/magenta system/ulib/c

MODULE_DEFINES := DDK_INTERNAL=1

include make/module.mk
