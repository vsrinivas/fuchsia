# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS := \
	lib/libc \
	lib/debug \
	lib/heap \
    lib/mxtl \


MODULE_SRCS := \
	$(LOCAL_DIR)/cond.c \
	$(LOCAL_DIR)/debug.c \
	$(LOCAL_DIR)/event.c \
	$(LOCAL_DIR)/init.c \
	$(LOCAL_DIR)/mutex.c \
	$(LOCAL_DIR)/thread.c \
	$(LOCAL_DIR)/timer.c \
	$(LOCAL_DIR)/semaphore.c \
	$(LOCAL_DIR)/mp.c \
	$(LOCAL_DIR)/cmdline.c \


ifeq ($(WITH_KERNEL_VM),1)
MODULE_DEPS += kernel/vm
else
MODULE_DEPS += kernel/novm
endif

include make/module.mk
