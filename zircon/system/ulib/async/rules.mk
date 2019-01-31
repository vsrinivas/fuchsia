# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
LOCAL_INC := $(LOCAL_DIR)/include/lib/async

#
# libasync.a: the client library
#

MODULE := $(LOCAL_DIR)
MODULE_NAME := async

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS = \
    $(LOCAL_DIR)/ops.c \

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := \
    $(LOCAL_INC)/dispatcher.h \
    $(LOCAL_INC)/exception.h \
    $(LOCAL_INC)/receiver.h \
    $(LOCAL_INC)/task.h \
    $(LOCAL_INC)/time.h \
    $(LOCAL_INC)/trap.h \
    $(LOCAL_INC)/wait.h \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon

MODULE_PACKAGE := src

include make/module.mk

#
# libasync-cpp.a: the C++ client library
#

MODULE := $(LOCAL_DIR).cpp
MODULE_NAME := async-cpp

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS = \
    $(LOCAL_DIR)/exception.cpp \
    $(LOCAL_DIR)/receiver.cpp \
    $(LOCAL_DIR)/task.cpp \
    $(LOCAL_DIR)/trap.cpp \
    $(LOCAL_DIR)/wait.cpp

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := \
    $(LOCAL_INC)/cpp/exception.h \
    $(LOCAL_INC)/cpp/receiver.h \
    $(LOCAL_INC)/cpp/task.h \
    $(LOCAL_INC)/cpp/time.h \
    $(LOCAL_INC)/cpp/trap.h \
    $(LOCAL_INC)/cpp/wait.h

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zx \
    system/ulib/zircon

MODULE_PACKAGE := src

include make/module.mk

#
# libasync-default.so: the default dispatcher state library
#

MODULE := $(LOCAL_DIR).default
MODULE_NAME := async-default

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS = \
    $(LOCAL_DIR)/default.c

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := $(LOCAL_INC)/default.h

MODULE_SO_NAME := async-default
MODULE_EXPORT := so

MODULE_LIBS := \
    system/ulib/c

MODULE_PACKAGE := src

include make/module.mk
