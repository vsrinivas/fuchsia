# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/main.cpp

MODULE_STATIC_LIBS := \
    system/ulib/trace-provider \
    system/ulib/trace \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/fbl \
    system/ulib/gfx \
    system/ulib/hid \
    system/ulib/framebuffer \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/fidl/fuchsia-display

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/trace-engine

include make/module.mk
