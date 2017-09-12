# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/audio.cpp \
    $(LOCAL_DIR)/sine-source.cpp \
    $(LOCAL_DIR)/wav-common.cpp \
    $(LOCAL_DIR)/wav-sink.cpp \
    $(LOCAL_DIR)/wav-source.cpp

MODULE_STATIC_LIBS := \
    system/ulib/audio-utils \
    system/ulib/audio-proto-utils \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c

include make/module.mk
