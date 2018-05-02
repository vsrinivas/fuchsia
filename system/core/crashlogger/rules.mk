# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/crashlogger.cpp \
    $(LOCAL_DIR)/dump-pt.cpp \

MODULE_NAME := crashlogger

MODULE_STATIC_LIBS := \
    system/ulib/inspector \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/pretty \
    system/ulib/runtime \
    system/ulib/task-utils

MODULE_LIBS := \
    third_party/ulib/backtrace \
    third_party/ulib/ngunwind \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c

# Compile this with frame pointers so that if we crash
# the simplistic unwinder will work.
MODULE_COMPILEFLAGS += $(KEEP_FRAME_POINTER_COMPILEFLAGS)

include make/module.mk
