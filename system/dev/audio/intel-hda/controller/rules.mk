# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/codec-cmd-job.cpp \
    $(LOCAL_DIR)/debug.cpp \
    $(LOCAL_DIR)/intel-hda-codec.cpp \
    $(LOCAL_DIR)/intel-hda-controller.cpp \
    $(LOCAL_DIR)/intel-hda-controller-init.cpp \
    $(LOCAL_DIR)/intel-hda-dsp.cpp \
    $(LOCAL_DIR)/intel-hda-stream.cpp \
    $(LOCAL_DIR)/irq-thread.cpp \
    $(LOCAL_DIR)/utils.cpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_STATIC_LIBS := \
    system/ulib/audio-driver-proto \
    system/ulib/ddk \
    system/ulib/intel-hda \
    system/ulib/audio-proto-utils \
    system/ulib/dispatcher-pool \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

include make/module.mk
