# Copyright 2017 The Fuchsia Authors. ALl rights reserved.
# Use of this source code is goverened by a BSD-style license that can be found
# in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := installer-tests

MODULE_TYPE := usertest

MODULE_SRCS += $(LOCAL_DIR)/tests.c

MODULE_STATIC_LIBS := ulib/gpt ulib/cksum ulib/lz4 ulib/installer

MODULE_LIBS := ulib/magenta ulib/musl ulib/mxio ulib/fs-management ulib/unittest

include make/module.mk
