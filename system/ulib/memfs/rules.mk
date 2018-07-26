# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
LOCAL_INC := $(LOCAL_DIR)/include/lib/memfs

#
# libmemfs-cpp.a: The C++ client library.
#
# Used to implement the C++ components of Memfs, which
# can be plugged into ulib/fs.
#

MODULE := $(LOCAL_DIR).cpp
MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_NAME := memfs-cpp

MODULE_SRCS := \
    $(LOCAL_DIR)/directory.cpp \
    $(LOCAL_DIR)/dnode.cpp \
    $(LOCAL_DIR)/file.cpp \
    $(LOCAL_DIR)/memfs.cpp \
    $(LOCAL_DIR)/vmo.cpp \

MODULE_PACKAGE_INCS := \
    $(LOCAL_INC)/cpp/vnode.h \

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_PACKAGE := src

include make/module.mk

#
# libmemfs.so: The C ABI client library.
#
# Used to create local temporary filesystems.
#

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_NAME := memfs

MODULE_SRCS := \
    $(LOCAL_DIR)/memfs-local.cpp \

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)

MODULE_PACKAGE_INCS := \
    $(LOCAL_INC)/memfs.h \

MODULE_STATIC_LIBS := \
    system/ulib/memfs.cpp \
    system/ulib/fs \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/fbl \
    system/ulib/trace \
    system/ulib/trace-engine \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_SO_NAME := $(MODULE_NAME)
MODULE_EXPORT := so
MODULE_PACKAGE := shared

include make/module.mk
