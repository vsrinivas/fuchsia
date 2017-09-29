# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ENABLE_FSHOST := true

LOCAL_DIR := $(GET_LOCAL_DIR)

LOCAL_FSHOST_SRCS := \
    $(LOCAL_DIR)/block-watcher.c \
    $(LOCAL_DIR)/dnode.cpp \
    $(LOCAL_DIR)/fshost.c \
    $(LOCAL_DIR)/vfs-memory.cpp \
    $(LOCAL_DIR)/vfs-rpc.cpp

LOCAL_FSHOST_STATIC_LIBS := \
    system/ulib/fs \
    system/ulib/fbl \
    system/ulib/async \
    system/ulib/async.loop \
    system/ulib/zx \
    system/ulib/zxcpp

LOCAL_FSHOST_LIBS := \
    system/ulib/async.default \
    system/ulib/fs-management \

MODULE := $(LOCAL_DIR)

# devmgr - core userspace services process
#
MODULE_NAME := devmgr

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/acpi.c \
    $(LOCAL_DIR)/devhost-shared.c \
    $(LOCAL_DIR)/devmgr.c \
    $(LOCAL_DIR)/devmgr-binding.c \
    $(LOCAL_DIR)/devmgr-coordinator.c \
    $(LOCAL_DIR)/devmgr-devfs.c \
    $(LOCAL_DIR)/devmgr-drivers.c \
    $(LOCAL_DIR)/devmgr-fdio.c

# userboot supports loading via the dynamic linker, so libc (system/ulib/c)
# can be linked dynamically.  But it doesn't support any means to look
# up other shared libraries, so everything else must be linked statically.

# ddk is needed only for ddk/device.h
MODULE_HEADER_DEPS := \
    system/ulib/ddk

MODULE_STATIC_LIBS := \
    system/ulib/gpt \
    system/ulib/bootdata \
    third_party/ulib/lz4 \
    system/ulib/port \
    system/ulib/acpisvc-client \
    system/ulib/driver-info \

MODULE_LIBS := \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c

ifeq ($(ENABLE_FSHOST),false)
MODULE_SRCS += $(LOCAL_FSHOST_SRCS)
MODULE_STATIC_LIBS += $(LOCAL_FSHOST_STATIC_LIBS)
MODULE_LIBS += $(LOCAL_FSHOST_LIBS)
else
MODULE_DEFINES := WITH_FSHOST=1
endif

include make/module.mk


ifeq ($(ENABLE_FSHOST),true)
# fshost - container for filesystems

MODULE := $(LOCAL_DIR).fshost

MODULE_NAME := fshost

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS := \
    $(LOCAL_DIR)/devmgr-fdio.c \
    $(LOCAL_FSHOST_SRCS)

MODULE_STATIC_LIBS := \
    system/ulib/gpt \
    system/ulib/bootdata \
    third_party/ulib/lz4 \
    $(LOCAL_FSHOST_STATIC_LIBS)

MODULE_LIBS := \
    $(LOCAL_FSHOST_LIBS) \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c

MODULE_DEFINES := WITH_FSHOST=1

include make/module.mk
endif

# devhost - container for drivers
#
# This is just a main() that calls device_host_main() which
# is provided by libdriver, where all the other devhost-*.c
# files get built.
#
MODULE := $(LOCAL_DIR).host

# The ASanified devhost is installed as devhost.asan so that
# devmgr can use the ASanified host for ASanified driver modules.
# TODO(mcgrathr): One day, both devhost and devhost.asan can both go
# into the same system image, independent of whether devmgr is ASanified.
ifeq ($(call TOBOOL,$(USE_ASAN)),true)
DEVHOST_SUFFIX := .asan
else
DEVHOST_SUFFIX :=
endif

MODULE_NAME := devhost$(DEVHOST_SUFFIX)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS := \
	$(LOCAL_DIR)/devhost-main.c

MODULE_LIBS := system/ulib/driver system/ulib/fdio system/ulib/c

include make/module.mk


# dmctl - bridge between dm command and devmgr

MODULE := $(LOCAL_DIR).dmctl

MODULE_TYPE := driver

MODULE_NAME := dmctl

MODULE_SRCS := \
	$(LOCAL_DIR)/dmctl.c \
	$(LOCAL_DIR)/devhost-shared.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/port

MODULE_LIBS := system/ulib/driver system/ulib/launchpad system/ulib/fdio system/ulib/zircon system/ulib/c

include make/module.mk
