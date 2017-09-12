# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += $(LOCAL_DIR)/decompress.c

MODULE_LIBS := \
    third_party/ulib/lz4 \
    system/ulib/zircon \
    system/ulib/c

include make/module.mk
