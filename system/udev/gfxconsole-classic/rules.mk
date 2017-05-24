# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifneq ($(ENABLE_NEW_FB),true)
LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/keyboard-vt100.cpp \
    $(LOCAL_DIR)/keyboard.cpp \
    $(LOCAL_DIR)/vc-device.cpp \
    $(LOCAL_DIR)/vc-gfx.cpp \
    $(LOCAL_DIR)/textcon.cpp \
    $(LOCAL_DIR)/main.cpp \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/gfx system/ulib/hid \
    system/ulib/mxcpp system/ulib/mxtl

MODULE_LIBS := system/ulib/driver system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_SRCS := \
    $(LOCAL_DIR)/keyboard-test.cpp \
    $(LOCAL_DIR)/keyboard-vt100.cpp \
    $(LOCAL_DIR)/keyboard.cpp \
    $(LOCAL_DIR)/vc-device.cpp \
    $(LOCAL_DIR)/vc-gfx.cpp \
    $(LOCAL_DIR)/textcon-test.cpp \
    $(LOCAL_DIR)/textcon.cpp \
    $(LOCAL_DIR)/main.cpp \

MODULE_NAME := gfxconsole-test

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/gfx system/ulib/hid system/ulib/mxcpp system/ulib/mxtl

MODULE_LIBS := system/ulib/driver system/ulib/unittest system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk

endif
