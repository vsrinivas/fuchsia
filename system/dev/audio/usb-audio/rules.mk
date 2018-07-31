# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/midi.c \
    $(LOCAL_DIR)/usb-audio.cpp \
    $(LOCAL_DIR)/usb-audio-control-interface.cpp \
    $(LOCAL_DIR)/usb-audio-descriptors.cpp \
    $(LOCAL_DIR)/usb-audio-device.cpp \
    $(LOCAL_DIR)/usb-audio-path.cpp \
    $(LOCAL_DIR)/usb-audio-stream.cpp \
    $(LOCAL_DIR)/usb-audio-stream-interface.cpp \
    $(LOCAL_DIR)/usb-audio-units.cpp \
    $(LOCAL_DIR)/usb-midi-sink.c \
    $(LOCAL_DIR)/usb-midi-source.c \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_STATIC_LIBS := \
    system/ulib/audio-driver-proto \
    system/ulib/audio-proto-utils \
    system/dev/lib/usb \
    system/ulib/digest \
    system/ulib/dispatcher-pool \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/pretty \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/uboringssl \

include make/module.mk
