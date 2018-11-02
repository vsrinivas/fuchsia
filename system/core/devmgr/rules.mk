# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# devmgr - core userspace services process
#
MODULE := $(LOCAL_DIR)

MODULE_NAME := devmgr
MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/bootfs.cpp \
    $(LOCAL_DIR)/devhost-shared.cpp \
    $(LOCAL_DIR)/devmgr.cpp \
    $(LOCAL_DIR)/devmgr-binding.cpp \
    $(LOCAL_DIR)/devmgr-coordinator.cpp \
    $(LOCAL_DIR)/devmgr-devfs.cpp \
    $(LOCAL_DIR)/devmgr-drivers.cpp \
    $(LOCAL_DIR)/devmgr-fdio.cpp

# userboot supports loading via the dynamic linker, so libc (system/ulib/c)
# can be linked dynamically.  But it doesn't support any means to look
# up other shared libraries, so everything else must be linked statically.

# We can avoid the fuchsia.crash dependency if crashsvc connects directly to the
# analyzer.
MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-crash \
    system/fidl/fuchsia-io \
    system/fidl/fuchsia-mem \

# ddk is needed only for ddk/device.h
MODULE_HEADER_DEPS := \
    system/ulib/ddk \
    system/ulib/zircon-internal

MODULE_STATIC_LIBS := \
    system/ulib/fidl \
    system/ulib/bootdata \
    system/ulib/loader-service \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/sync \
    third_party/ulib/lz4 \
    system/ulib/port \
    system/ulib/driver-info \
    system/ulib/memfs \
    system/ulib/fs \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c

include make/module.mk


# fshost - container for filesystems

MODULE := $(LOCAL_DIR).fshost

MODULE_NAME := fshost
MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS := \
    $(LOCAL_DIR)/bootfs.cpp \
    $(LOCAL_DIR)/block-watcher.cpp \
    $(LOCAL_DIR)/devmgr-fdio.cpp \
    $(LOCAL_DIR)/fshost.cpp \
    $(LOCAL_DIR)/vfs-rpc.cpp

MODULE_STATIC_LIBS := \
    system/ulib/memfs.cpp \
    system/ulib/memfs \
    system/ulib/fs \
    system/ulib/loader-service \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/bootdata \
    system/ulib/fbl \
    system/ulib/gpt \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/cksum \
    third_party/ulib/lz4 \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/fs-management \
    system/ulib/trace-engine \
    system/ulib/zircon \
    system/ulib/c

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \

include make/module.mk


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
	$(LOCAL_DIR)/devhost-main.cpp

MODULE_LIBS := system/ulib/driver system/ulib/fdio system/ulib/c

include make/module.mk


# dmctl - bridge between dm command and devmgr

MODULE := $(LOCAL_DIR).dmctl

MODULE_TYPE := driver

MODULE_NAME := dmctl

MODULE_SRCS := \
	$(LOCAL_DIR)/dmctl.cpp \
	$(LOCAL_DIR)/devhost-shared.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/port \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
