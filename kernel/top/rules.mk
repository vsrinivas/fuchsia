# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS := \
	platform \
	target \
	app \
	dev \
	kernel

MODULE_SRCS := \
	$(LOCAL_DIR)/init.c \
	$(LOCAL_DIR)/main.c \

include make/module.mk
