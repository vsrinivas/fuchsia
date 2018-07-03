# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := test

MODULE_SRCS += \
    $(LOCAL_DIR)/runtests.cpp \

MODULE_NAME := runtests

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-logger

# zxcpp required for fbl to work.
MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async.cpp \
    system/ulib/async-loop.cpp \
    system/ulib/fidl \
    system/ulib/fbl \
    system/ulib/runtests-utils \
    system/ulib/zx \
    system/ulib/zxcpp \

# We have to include all MODULE_LIBS from runtests-utils because transitive dependencies don't
# get linked in automatically :-(.
MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/launchpad \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
