# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_FIRMWARE := intel-adsp-sst/dsp_fw_kbl_v3266.bin

MODULE_SRCS += \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/debug.cpp \
    $(LOCAL_DIR)/intel-audio-dsp.cpp \
    $(LOCAL_DIR)/intel-dsp-code-loader.cpp \
    $(LOCAL_DIR)/intel-dsp-ipc.cpp \
    $(LOCAL_DIR)/intel-dsp-stream.cpp

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_STATIC_LIBS := \
    system/ulib/intel-hda \
    system/ulib/audio-driver-proto \
    system/ulib/audio-proto-utils \
    system/ulib/ddk \
    system/ulib/dispatcher-pool \
    system/ulib/fbl \
    system/ulib/pretty \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

include make/module.mk
