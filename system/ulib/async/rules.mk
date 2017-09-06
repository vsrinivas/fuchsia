# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# libasync.a: the client library
#

MODULE := $(LOCAL_DIR)
MODULE_NAME := async

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/auto_task.cpp \
    $(LOCAL_DIR)/auto_wait.cpp \
    $(LOCAL_DIR)/receiver.cpp \
    $(LOCAL_DIR)/task.cpp \
    $(LOCAL_DIR)/wait.cpp \
    $(LOCAL_DIR)/wait_with_timeout.cpp

MODULE_STATIC_LIBS := \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta

include make/module.mk

#
# libasync-loop.a: the message loop library
#

MODULE := $(LOCAL_DIR).loop
MODULE_NAME := async-loop

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/loop.c \
    $(LOCAL_DIR)/loop_wrapper.cpp

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/magenta

include make/module.mk

#
# libasync-default.so: the default dispatcher state library
#

MODULE := $(LOCAL_DIR).default
MODULE_NAME := async-default

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/default.c

MODULE_SO_NAME := async-default
MODULE_EXPORT := so

MODULE_LIBS := \
    system/ulib/c

MODULE_STATIC_LIBS := \
    system/ulib/fbl

include make/module.mk
