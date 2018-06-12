# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# spawn-test
#

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := spawn-test

MODULE_SRCS := \
    $(LOCAL_DIR)/spawn.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/c \
    system/ulib/zircon \

include make/module.mk

#
# spawn-child
#

MODULE := $(LOCAL_DIR).child

MODULE_TYPE := userapp
MODULE_GROUP := test

MODULE_NAME := spawn-child

MODULE_SRCS := \
    $(LOCAL_DIR)/child.c \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/c \
    system/ulib/zircon \

include make/module.mk

#
# spawn-launcher
#

MODULE := $(LOCAL_DIR).launcher

MODULE_TYPE := userapp
MODULE_GROUP := test

MODULE_NAME := spawn-launcher

MODULE_SRCS := \
    $(LOCAL_DIR)/launcher.c \

MODULE_LIBS := \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/c \
    system/ulib/zircon \

include make/module.mk
