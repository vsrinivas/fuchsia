# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

# devmgr - core userspace services process
#
MODULE_NAME := devmgr

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/acpi.c \
    $(LOCAL_DIR)/block-watcher.c \
    $(LOCAL_DIR)/dnode.cpp \
    $(LOCAL_DIR)/devhost-shared.c \
    $(LOCAL_DIR)/devmgr.c \
    $(LOCAL_DIR)/devmgr-binding.c \
    $(LOCAL_DIR)/devmgr-coordinator.c \
    $(LOCAL_DIR)/devmgr-devfs.c \
    $(LOCAL_DIR)/devmgr-drivers.c \
    $(LOCAL_DIR)/devmgr-mxio.c \
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
    system/ulib/fs \
    system/ulib/async \
    system/ulib/async.loop \
    system/ulib/gpt \
    system/ulib/mx \
    system/ulib/bootdata \
    third_party/ulib/lz4 \
    system/ulib/mxcpp \
    system/ulib/fbl \
    system/ulib/port \
    system/ulib/acpisvc-client \
    system/ulib/driver-info \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/fs-management \
    system/ulib/launchpad \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

include make/module.mk


# devhost - container for drivers
#
# This is just a main() that calls device_host_main() which
# is provided by libdriver, where all the other devhost-*.c
# files get built.
#
MODULE := $(LOCAL_DIR).host

MODULE_NAME := devhost

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS := \
	$(LOCAL_DIR)/devhost-main.c

MODULE_LIBS := system/ulib/driver system/ulib/mxio system/ulib/c

include make/module.mk


# dmctl - bridge between dm command and devmgr

MODULE := $(LOCAL_DIR).dmctl

MODULE_TYPE := driver

MODULE_NAME := dmctl

MODULE_SRCS := \
	$(LOCAL_DIR)/dmctl.c \
	$(LOCAL_DIR)/devhost-shared.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/port

MODULE_LIBS := system/ulib/driver system/ulib/launchpad system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk
