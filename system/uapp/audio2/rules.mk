# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/audio2.cpp \
    $(LOCAL_DIR)/audio-input.cpp \
    $(LOCAL_DIR)/audio-output.cpp \
    $(LOCAL_DIR)/audio-stream.cpp \
    $(LOCAL_DIR)/sine-source.cpp \
    $(LOCAL_DIR)/wav-source.cpp

MODULE_STATIC_LIBS := \
    system/ulib/mx \
    system/ulib/mxalloc \
    system/ulib/mxcpp \
    system/ulib/mxtl

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

include make/module.mk
