# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/audio-stream-in.cpp \

MODULE_LIBS := \
  system/ulib/c \
  system/ulib/driver \
  system/ulib/zircon \

MODULE_STATIC_LIBS := \
  system/dev/audio/lib/simple-audio-stream \
  system/dev/lib/amlogic \
  system/dev/lib/mmio \
  system/ulib/audio-proto-utils \
  system/ulib/audio-driver-proto \
  system/ulib/ddk \
  system/ulib/ddktl \
  system/ulib/dispatcher-pool \
  system/ulib/fbl \
  system/ulib/fzl \
  system/ulib/sync \
  system/ulib/zx \
  system/ulib/zxcpp \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-clk \
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-i2c \
    system/banjo/ddk-protocol-platform-device \

MODULE_HEADER_DEPS := system/dev/lib/amlogic

include make/module.mk
