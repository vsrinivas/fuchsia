# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
LOCAL_INC := $(LOCAL_DIR)/include/debug-ipc

#
# Target userspace library.
#

MODULE := $(LOCAL_DIR)
MODULE_NAME := debug-ipc

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/message_reader.cpp \
    $(LOCAL_DIR)/message_writer.cpp \

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := \
    $(LOCAL_INC)/message_reader.h \
    $(LOCAL_INC)/message_writer.h \
    $(LOCAL_INC)/protocol.h \
    $(LOCAL_INC)/records.h \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fbl \

MODULE_PACKAGE := src

include make/module.mk

#
# Host library.
#

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS = \
    $(LOCAL_DIR)/message_reader.cpp \
    $(LOCAL_DIR)/message_writer.cpp \

MODULE_HOST_SRCS := $(MODULE_SRCS)
MODULE_HOST_INCS := \
    $(LOCAL_INC)/message_reader.h \
    $(LOCAL_INC)/message_writer.h \
    $(LOCAL_INC)/protocol.h \
    $(LOCAL_INC)/records.h \

MODULE_HOST_LIBS := \
    system/ulib/fbl \

include make/module.mk

#
# Host unit test library.
#

# TODO(brettw) figure out unit testing for this host code.
#
#MODULE := $(LOCAL_DIR).test.hostlib
#
#MODULE_TYPE := hostlib
#
#MODULE_SRCS = \
#    $(LOCAL_DIR)/message_reader.cpp \
#    $(LOCAL_DIR)/message_writer.cpp \
#
#include make/module.mk
