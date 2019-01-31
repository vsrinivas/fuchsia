# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

# The C file here uses a macro to detect whether a type is signed, by
# comparing the values of -1 and 0. This leads to complaints about
# vacuously true comparisons, which we don't care about.
MODULE_COMPILEFLAGS := -Wno-type-limits

MODULE_SRCS := \
    $(LOCAL_DIR)/int-types.c \
    $(LOCAL_DIR)/int-types.cpp \
    $(LOCAL_DIR)/wchar-type.c \
    $(LOCAL_DIR)/wchar-type.cpp \

MODULE_NAME := int-types-test

MODULE_STATIC_LIBS := system/ulib/fbl

MODULE_LIBS := system/ulib/unittest system/ulib/fdio system/ulib/c

include make/module.mk
