# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/a113-audio-device.c \
    $(LOCAL_DIR)/a113-ddr.c \
    $(LOCAL_DIR)/a113-pdm.c \
    $(LOCAL_DIR)/gauss-pdm-input.c \
    $(LOCAL_DIR)/gauss-pdm-input-stream.cpp \
    $(LOCAL_DIR)/vmo_helper.cpp \

MODULE_LIBS := \
  system/ulib/c \
  system/ulib/driver \
  system/ulib/zircon \

MODULE_STATIC_LIBS := \
  system/ulib/audio-proto-utils \
  system/ulib/audio-driver-proto \
  system/ulib/ddk \
  system/ulib/ddktl \
  system/ulib/dispatcher-pool \
  system/ulib/fbl \
  system/ulib/sync \
  system/ulib/zx \
  system/ulib/zxcpp \

include make/module.mk
