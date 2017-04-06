# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/audio-util.c \
    $(LOCAL_DIR)/midi.c \
    $(LOCAL_DIR)/usb-audio.c \
    $(LOCAL_DIR)/usb-audio-sink.c \
    $(LOCAL_DIR)/usb-audio-source.c \
    $(LOCAL_DIR)/usb-midi-sink.c \
    $(LOCAL_DIR)/usb-midi-source.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/magenta system/ulib/c

include make/module.mk
