# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest
MODULE_GROUP := disktools
MODULE_NAME := chromeos-disk-setup-test

MODULE_SRCS := $(LOCAL_DIR)/chromeos-disk-setup.c

MODULE_STATIC_LIBS := \
  system/ulib/chromeos-disk-setup \
  system/ulib/gpt \
  third_party/ulib/cksum

MODULE_LIBS := \
  system/ulib/c \
  system/ulib/fdio \
  system/ulib/fs-management \
  system/ulib/unittest \
  system/ulib/zircon

include make/module.mk
