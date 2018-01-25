# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/connection.cpp \
    $(LOCAL_DIR)/fvm.cpp \
    $(LOCAL_DIR)/managed-vfs.cpp \
    $(LOCAL_DIR)/mapped-vmo.cpp \
    $(LOCAL_DIR)/mount.cpp \
    $(LOCAL_DIR)/pseudo-dir.cpp \
    $(LOCAL_DIR)/pseudo-file.cpp \
    $(LOCAL_DIR)/remote-dir.cpp \
    $(LOCAL_DIR)/service.cpp \
    $(LOCAL_DIR)/unmount.cpp \
    $(LOCAL_DIR)/vfs.cpp \
    $(LOCAL_DIR)/vmo-file.cpp \
    $(LOCAL_DIR)/vnode.cpp \
    $(LOCAL_DIR)/watcher.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/fidl \
    system/ulib/trace-engine \
    system/ulib/zircon \

MODULE_PACKAGE := src

include make/module.mk
