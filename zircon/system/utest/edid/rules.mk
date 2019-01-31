# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).fuzzer

MODULE_TYPE := fuzztest

MODULE_SRCS = \
    $(LOCAL_DIR)/edid-fuzztest.cpp

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/edid \
    system/ulib/fbl \
    system/ulib/hwreg \
    system/ulib/zx

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/c \

include make/module.mk
