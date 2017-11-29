# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_GROUP := disktools

MODULE_SRCS := $(LOCAL_DIR)/chromeos-disk-setup.c

MODULE_LIBS := system/ulib/c

MODULE_STATIC_LIBS := system/ulib/gpt \
    system/ulib/zircon

include make/module.mk
