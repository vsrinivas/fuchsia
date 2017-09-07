# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += $(LOCAL_DIR)/tftp.c

MODULE_EXPORT := a

#MODULE_SO_NAME := tftp
MODULE_LIBS := system/ulib/magenta system/ulib/c

MODULE_COMPILEFLAGS := -DTFTP_USERLIB

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_SRCS := $(LOCAL_DIR)/tftp-test.cpp $(LOCAL_DIR)/tftp-file-test.c

MODULE_NAME := tftp-test

MODULE_STATIC_LIBS := system/ulib/tftp system/ulib/fbl system/ulib/mxcpp

MODULE_LIBS := system/ulib/unittest system/ulib/mxio system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).tftp-example

MODULE_TYPE := hostapp

MODULE_SRCS := $(LOCAL_DIR)/tftp.c $(LOCAL_DIR)/tftp-example.c

MODULE_NAME := tftp-example

MODULE_COMPILEFLAGS := -I$(LOCAL_DIR)/include -std=c11 -DTFTP_HOSTLIB

include make/module.mk

MODULE := $(LOCAL_DIR).hostlib

MODULE_NAME := tftp

MODULE_TYPE := hostlib

MODULE_SRCS := $(LOCAL_DIR)/tftp.c

MODULE_COMPILEFLAGS := -DTFTP_HOSTLIB

include make/module.mk

MODULE := $(LOCAL_DIR).efilib

MODULE_NAME := tftp

MODULE_TYPE := efilib

MODULE_SRCS := $(LOCAL_DIR)/tftp.c

MODULE_COMPILEFLAGS := -DTFTP_EFILIB

include make/module.mk
