# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/mapped-vmo.cpp \
    $(LOCAL_DIR)/mxio-dispatcher.cpp \
    $(LOCAL_DIR)/vfs.cpp \
    $(LOCAL_DIR)/vfs-mount.cpp \
    $(LOCAL_DIR)/vfs-unmount.cpp \
    $(LOCAL_DIR)/vfs-rpc.cpp \
    $(LOCAL_DIR)/vfs-dispatcher.cpp \
    $(LOCAL_DIR)/vfs-watcher.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/mx \
    system/ulib/mxalloc \
    system/ulib/mxcpp \
    system/ulib/mxtl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \

include make/module.mk
