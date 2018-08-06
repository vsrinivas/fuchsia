# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS := \
	kernel/lib/counters \
	kernel/lib/debug \
	kernel/lib/explicit-memory \
	kernel/lib/heap \
	kernel/lib/libc \
	kernel/lib/fbl \
	kernel/lib/zircon-internal \
	kernel/vm

MODULE_SRCS := \
	$(LOCAL_DIR)/cmdline.cpp \
	$(LOCAL_DIR)/debug.cpp \
	$(LOCAL_DIR)/dpc.cpp \
	$(LOCAL_DIR)/event.cpp \
	$(LOCAL_DIR)/init.cpp \
	$(LOCAL_DIR)/mp.cpp \
	$(LOCAL_DIR)/mutex.cpp \
	$(LOCAL_DIR)/percpu.cpp \
	$(LOCAL_DIR)/sched.cpp \
	$(LOCAL_DIR)/thread.cpp \
	$(LOCAL_DIR)/timer.cpp \
	$(LOCAL_DIR)/wait.cpp

include make/module.mk
