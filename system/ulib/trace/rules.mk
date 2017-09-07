# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/event.cpp \
    $(LOCAL_DIR)/handler.cpp \
    $(LOCAL_DIR)/observer.cpp

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/mx \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/trace-engine

include make/module.mk
