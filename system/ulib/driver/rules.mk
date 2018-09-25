# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

DEVMGR_SRCS := system/core/devmgr

MODULE := $(LOCAL_DIR)

MODULE_NAME := driver

MODULE_TYPE := userlib

MODULE_EXPORT := so
MODULE_SO_NAME := driver

MODULE_COMPILEFLAGS := -fvisibility=hidden

MODULE_SRCS := \
	$(DEVMGR_SRCS)/devhost.cpp \
	$(DEVMGR_SRCS)/devhost-api.cpp \
	$(DEVMGR_SRCS)/devhost-core.cpp \
	$(DEVMGR_SRCS)/devhost-rpc-server.cpp \
	$(DEVMGR_SRCS)/devhost-shared.cpp \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/sync \
    system/ulib/port \

MODULE_LIBS := system/ulib/fdio system/ulib/zircon system/ulib/c

include make/module.mk
