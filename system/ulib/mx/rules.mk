# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

# Don't forget to update BUILD.gn as well for the Fuchsia build.
MODULE_SRCS += \
    $(LOCAL_DIR)/channel.cpp \
    $(LOCAL_DIR)/event.cpp \
    $(LOCAL_DIR)/eventpair.cpp \
    $(LOCAL_DIR)/fifo.cpp \
    $(LOCAL_DIR)/job.cpp \
    $(LOCAL_DIR)/log.cpp \
    $(LOCAL_DIR)/port.cpp \
    $(LOCAL_DIR)/process.cpp \
    $(LOCAL_DIR)/socket.cpp \
    $(LOCAL_DIR)/thread.cpp \
    $(LOCAL_DIR)/timer.cpp \
    $(LOCAL_DIR)/vmar.cpp \
    $(LOCAL_DIR)/vmo.cpp \

MODULE_LIBS := system/ulib/magenta

include make/module.mk
