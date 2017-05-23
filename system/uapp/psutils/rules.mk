# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).ps

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/ps.c

MODULE_NAME := ps

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

MODULE_STATIC_LIBS := \
    system/ulib/pretty \
    system/ulib/task-utils

include make/module.mk

MODULE := $(LOCAL_DIR).top

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/top.c

MODULE_NAME := top

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

MODULE_STATIC_LIBS := \
    system/ulib/pretty \
    system/ulib/task-utils

include make/module.mk

MODULE := $(LOCAL_DIR).kill

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/kill.c

MODULE_NAME := kill

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

MODULE_STATIC_LIBS := \
    system/ulib/task-utils

include make/module.mk

MODULE := $(LOCAL_DIR).killall

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/killall.c

MODULE_NAME := killall

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

MODULE_STATIC_LIBS := \
    system/ulib/task-utils

include make/module.mk

MODULE := $(LOCAL_DIR).vmaps

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/vmaps.c

MODULE_NAME := vmaps

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

MODULE_STATIC_LIBS := \
    system/ulib/pretty \
    system/ulib/task-utils

include make/module.mk
