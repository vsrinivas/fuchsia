# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

# The Magenta makefile system is not really friendly to defining multiple
# targets in a single directory's rules.mk, so we define this test
# executable under system/utest/ but refer back to source files in
# system/udev/.
MODULE_SRCS += \
    system/udev/gfxconsole/textcon-test.cpp \
    system/udev/gfxconsole/textcon.cpp \

MODULE_NAME := textcon-test

MODULE_STATIC_LIBS := ulib/mxcpp

MODULE_LIBS := ulib/unittest ulib/mxio ulib/magenta ulib/musl

include make/module.mk
