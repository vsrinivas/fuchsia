# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/new.cpp \
    $(LOCAL_DIR)/pure_virtual.cpp \
    $(LOCAL_DIR)/thread_atexit.cpp \

MODULE_STATIC_LIBS += system/ulib/c

include make/module.mk
