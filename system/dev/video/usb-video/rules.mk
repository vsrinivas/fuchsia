# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/usb-video.cpp \
    $(LOCAL_DIR)/usb-video-stream.cpp \
    $(LOCAL_DIR)/video-buffer.cpp \
    $(LOCAL_DIR)/video-util.c \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/dispatcher-pool \
    system/ulib/fbl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

include make/module.mk
