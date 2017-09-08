# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/async-dispatcher.cpp \
    $(LOCAL_DIR)/mapped-vmo.cpp \
    $(LOCAL_DIR)/vfs.cpp \
    $(LOCAL_DIR)/mount.cpp \
    $(LOCAL_DIR)/unmount.cpp \
    $(LOCAL_DIR)/rpc.cpp \
    $(LOCAL_DIR)/watcher.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.loop \
    system/ulib/mx \
    system/ulib/mxcpp \
    system/ulib/fbl \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \

include make/module.mk
