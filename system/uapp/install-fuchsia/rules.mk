# Copyright 2016 The Fuchsia Authors. ALl rights reserved.
# Use of this source code is goverened by a BSD-style license that can be found
# in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += $(LOCAL_DIR)/install-fuchsia.c

MODULE_STATIC_LIBS := ulib/gpt ulib/cksum

MODULE_LIBS := ulib/magenta ulib/musl ulib/mxio

include make/module.mk
