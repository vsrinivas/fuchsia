# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/intel_hda_device.cpp \
    $(LOCAL_DIR)/print_codec_state.cpp \
    $(LOCAL_DIR)/intel_hda_codec.cpp \
    $(LOCAL_DIR)/ihda.cpp \
    $(LOCAL_DIR)/zircon_device.cpp \
    $(LOCAL_DIR)/intel_hda_controller.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/intel-hda \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c

include make/module.mk
