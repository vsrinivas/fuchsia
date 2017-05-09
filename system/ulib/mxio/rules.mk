# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/bootfs.c \
    $(LOCAL_DIR)/bsdsocket.c \
    $(LOCAL_DIR)/dispatcher.c \
    $(LOCAL_DIR)/epoll.c \
    $(LOCAL_DIR)/logger.c \
    $(LOCAL_DIR)/namespace.c \
    $(LOCAL_DIR)/null.c \
    $(LOCAL_DIR)/pipe.c \
    $(LOCAL_DIR)/vmofile.c \
    $(LOCAL_DIR)/remoteio.c \
    $(LOCAL_DIR)/remotesocket.c \
    $(LOCAL_DIR)/unistd.c \
    $(LOCAL_DIR)/service.c \
    $(LOCAL_DIR)/stubs.c \
    $(LOCAL_DIR)/loader-service.c \
    $(LOCAL_DIR)/uname.c \
    $(LOCAL_DIR)/waitable.c \
    $(LOCAL_DIR)/watcher.c \
    $(LOCAL_DIR)/get-vmo.c \

MODULE_EXPORT := so

MODULE_SO_NAME := mxio
MODULE_LIBS := system/ulib/magenta system/ulib/c

include make/module.mk
