# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
LOCAL_INC := $(LOCAL_DIR)/include/async

#
# libasync.a: the client library
#

MODULE := $(LOCAL_DIR)
MODULE_NAME := async

MODULE_TYPE := userlib

MODULE_SRCS =

MODULE_PACKAGE_SRCS := none
MODULE_PACKAGE_INCS := \
    $(LOCAL_INC)/dispatcher.h \
    $(LOCAL_INC)/receiver.h \
    $(LOCAL_INC)/task.h \
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

MODULE_SRCS = \
    $(LOCAL_DIR)/auto_task.cpp \
    $(LOCAL_DIR)/auto_wait.cpp \
    $(LOCAL_DIR)/receiver.cpp \
    $(LOCAL_DIR)/task.cpp \
    $(LOCAL_DIR)/wait.cpp \
    $(LOCAL_DIR)/wait_with_timeout.cpp

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := \
    $(LOCAL_INC)/cpp/auto_task.h \
    $(LOCAL_INC)/cpp/auto_wait.h \
    $(LOCAL_INC)/cpp/receiver.h \
    $(LOCAL_INC)/cpp/task.h \
    $(LOCAL_INC)/cpp/wait.h \
    $(LOCAL_INC)/cpp/wait_with_timeout.h

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon

MODULE_PACKAGE := src

include make/module.mk

#
# libasync-loop.a: the message loop library
#

MODULE := $(LOCAL_DIR).loop
MODULE_NAME := async-loop

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/loop.c

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := $(LOCAL_INC)/loop.h

MODULE_STATIC_LIBS := \
    system/ulib/async

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/zircon

MODULE_PACKAGE := src

include make/module.mk

#
# libasync-loop-cpp.a: the message loop library
#

MODULE := $(LOCAL_DIR).loop-cpp
MODULE_NAME := async-loop-cpp

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/loop_wrapper.cpp

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := $(LOCAL_INC)/loop.h

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async.loop \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/zircon

MODULE_PACKAGE := src

include make/module.mk

#
# libasync-default.so: the default dispatcher state library
#

MODULE := $(LOCAL_DIR).default
MODULE_NAME := async-default

MODULE_TYPE := userlib

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
