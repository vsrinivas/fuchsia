# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/vfs.cpp \
    $(LOCAL_DIR)/vfs-mount.cpp \
    $(LOCAL_DIR)/vfs-unmount.cpp \
    $(LOCAL_DIR)/vfs-rpc.cpp \

MODULE_LIBS := \
    ulib/c \
    ulib/magenta \
    ulib/mxcpp \
    ulib/mxio \
    ulib/mxtl \

include make/module.mk
