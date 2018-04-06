# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/dlfcn.c

MODULE_NAME := dlfcn-test

# This test uses liblaunchpad.so as the test library to dlopen.
# We don't want it to already be there when we call dlopen, but
# we use launchpad_vmo_from_file to load it!  So link it statically.
MODULE_STATIC_LIBS := \
    system/ulib/launchpad \
    system/ulib/loader-service \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/ldmsg \
    system/ulib/elfload \

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/async.default \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c \

include make/module.mk
