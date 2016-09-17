# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := system/uapp/devmgr

MODULE := system/ulib/driver

MODULE_NAME := driver

MODULE_TYPE := userlib

MODULE_SO_NAME := driver

MODULE_DEFINES := LIBDRIVER=1

MODULE_COMPILEFLAGS := -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/devhost.c \
    $(LOCAL_DIR)/devhost-api.c \
    $(LOCAL_DIR)/devhost-binding.c \
    $(LOCAL_DIR)/devhost-core.c \
    $(LOCAL_DIR)/devhost-rpc-server.c \
    system/udev/kpci/kpci.c \

MODULE_STATIC_LIBS := ulib/acpisvc-client ulib/ddk

MODULE_LIBS := ulib/mxio ulib/launchpad ulib/magenta ulib/musl

include make/module.mk
