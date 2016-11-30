# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

# Don't forget to update BUILD.gn as well for the Fuchsia build.
MODULE_SRCS += \
    $(LOCAL_DIR)/channel.cc \
    $(LOCAL_DIR)/event.cc \
    $(LOCAL_DIR)/eventpair.cc \
    $(LOCAL_DIR)/job.cc \
    $(LOCAL_DIR)/log.cc \
    $(LOCAL_DIR)/port.cc \
    $(LOCAL_DIR)/process.cc \
    $(LOCAL_DIR)/socket.cc \
    $(LOCAL_DIR)/thread.cc \
    $(LOCAL_DIR)/vmar.cc \
    $(LOCAL_DIR)/vmo.cc \
    $(LOCAL_DIR)/waitset.cc \

MODULE_SO_NAME := libmx

MODULE_LIBS := ulib/magenta

include make/module.mk
