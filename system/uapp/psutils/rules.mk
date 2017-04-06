# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).ps

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/ps.c $(LOCAL_DIR)/processes.c $(LOCAL_DIR)/format.c

MODULE_NAME := ps

MODULE_LIBS := system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).kill

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/kill.c $(LOCAL_DIR)/processes.c

MODULE_NAME := kill

MODULE_LIBS := system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).killall

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/killall.c $(LOCAL_DIR)/processes.c

MODULE_NAME := killall

MODULE_LIBS := system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).vmaps

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/format.c $(LOCAL_DIR)/processes.c $(LOCAL_DIR)/vmaps.c

MODULE_NAME := vmaps

MODULE_LIBS := system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/format.c \
    $(LOCAL_DIR)/test.c

MODULE_NAME := psutils-test

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/mxio \
    system/ulib/c

include make/module.mk
