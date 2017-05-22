# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS := \
	kernel/lib/debug \
	kernel/lib/dpc \
	kernel/lib/heap \
	kernel/lib/libc \
	kernel/lib/mxtl \


MODULE_SRCS := \
	$(LOCAL_DIR)/debug.c \
	$(LOCAL_DIR)/event.c \
	$(LOCAL_DIR)/init.c \
	$(LOCAL_DIR)/mutex.c \
	$(LOCAL_DIR)/sched.c \
	$(LOCAL_DIR)/thread.c \
	$(LOCAL_DIR)/timer.c \
	$(LOCAL_DIR)/mp.c \
	$(LOCAL_DIR)/cmdline.c \

MODULE_DEPS += kernel/kernel/vm

MDI_INCLUDES += kernel/include/mdi/kernel-defs.mdi

include make/module.mk
