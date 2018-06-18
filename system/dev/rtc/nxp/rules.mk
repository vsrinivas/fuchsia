# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/pcf8563-rtc.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/hid \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/sync \
    system/ulib/hid

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/rtc system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
