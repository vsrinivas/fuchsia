# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(ARCH),x86)

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/bind.c \
    $(LOCAL_DIR)/display-device.cpp \
    $(LOCAL_DIR)/dp-display.cpp \
    $(LOCAL_DIR)/gtt.cpp \
    $(LOCAL_DIR)/hdmi-display.cpp \
    $(LOCAL_DIR)/igd.cpp \
    $(LOCAL_DIR)/intel-i915.cpp \
    $(LOCAL_DIR)/interrupts.cpp \
    $(LOCAL_DIR)/pipe.cpp \
    $(LOCAL_DIR)/power.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/edid \
    system/ulib/fbl \
    system/ulib/hwreg \
    system/ulib/region-alloc \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk

endif
