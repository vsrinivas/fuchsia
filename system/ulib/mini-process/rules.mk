# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)


# mini-process library: This runs in a parent process, which launches a
# subprocess.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/mini-process.c \

MODULE_SO_NAME := mini-process

MODULE_STATIC_LIBS := system/ulib/elfload

MODULE_LIBS := system/ulib/magenta system/ulib/c system/ulib/mxio

MODULE_COMPILEFLAGS += $(NO_SANCOV)

include make/module.mk


# mini-process-subprocess: Program that runs in the subprocess.
#
# This is built as a ".so" library to avoid linking in the crt1.o that is
# used for executables, which defines a _start() function that we don't
# want here because this program defines its own _start().
#
# Although this program is given a ".so" filename, it is more like an
# executable.

MODULE := $(LOCAL_DIR).subprocess

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/subprocess.c \

MODULE_EXPORT := so

MODULE_SO_NAME := mini-process-subprocess

MODULE_COMPILEFLAGS += $(NO_SANCOV)

include make/module.mk
