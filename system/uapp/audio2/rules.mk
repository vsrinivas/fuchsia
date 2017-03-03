# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/audio2.cpp \
    $(LOCAL_DIR)/audio-output.cpp \
    $(LOCAL_DIR)/sine-source.cpp \
    $(LOCAL_DIR)/wav-source.cpp

MODULE_LIBS := ulib/c \
               ulib/magenta \
               ulib/mxcpp \
               ulib/mxio \
               ulib/mx \
               ulib/mxtl

include make/module.mk
