# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := $(LOCAL_DIR)/mdns.c

MODULE_EXPORT := a

MODULE_LIBS := system/ulib/zircon system/ulib/c

MODULE_COMPILEFLAGS := -DMDNS_USERLIB

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_SRCS := $(LOCAL_DIR)/mdns-test.cpp

MODULE_NAME := mdns-test

MODULE_STATIC_LIBS := system/ulib/mdns

MODULE_LIBS := system/ulib/unittest system/ulib/fdio system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).hostlib

MODULE_NAME := mdns

MODULE_TYPE := hostlib

MODULE_SRCS := $(LOCAL_DIR)/mdns.c

MODULE_COMPILEFLAGS := -DMDNS_HOSTLIB

include make/module.mk

MODULE := $(LOCAL_DIR).efilib

MODULE_NAME := mdns

MODULE_TYPE := efilib

MODULE_SRCS := $(LOCAL_DIR)/mdns.c

MODULE_COMPILEFLAGS := -DMDNS_EFILIB

include make/module.mk
