# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/fallback-rtc.c \
    $(LOCAL_DIR)/rtc-impl.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/rtc

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
