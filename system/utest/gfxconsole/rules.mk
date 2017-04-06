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
    system/udev/gfxconsole/keyboard-vt100.cpp \
    system/udev/gfxconsole/keyboard.cpp \
    system/udev/gfxconsole/main.cpp \
    system/udev/gfxconsole/textcon-test.cpp \
    system/udev/gfxconsole/textcon.cpp \
    system/udev/gfxconsole/vc-device.cpp \
    system/udev/gfxconsole/vc-gfx.cpp \

MODULE_NAME := textcon-test

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/gfx system/ulib/hid system/ulib/mxcpp system/ulib/mxtl

MODULE_LIBS := system/ulib/driver system/ulib/unittest system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk
