# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/codec-utils/codec-driver-base.cpp \
    $(LOCAL_DIR)/codec-utils/stream-base.cpp \
    $(LOCAL_DIR)/utils/codec-caps.cpp \
    $(LOCAL_DIR)/utils/utils.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/audio-proto-utils \
    system/ulib/audio-driver-proto \
    system/ulib/ddk \
    system/ulib/dispatcher-pool \
    system/ulib/fbl \
    system/ulib/zx \

include make/module.mk
