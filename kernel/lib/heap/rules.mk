# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

GLOBAL_INCLUDES += $(LOCAL_DIR)/include

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/heap_wrapper.c \
	$(LOCAL_DIR)/page_alloc.c \
	$(LOCAL_DIR)/new.cpp

# pick a heap implementation
ifndef LK_HEAP_IMPLEMENTATION
LK_HEAP_IMPLEMENTATION=miniheap
endif
ifeq ($(LK_HEAP_IMPLEMENTATION),miniheap)
MODULE_DEPS := lib/heap/miniheap
endif
ifeq ($(LK_HEAP_IMPLEMENTATION),cmpctmalloc)
MODULE_DEPS := lib/heap/cmpctmalloc
endif

GLOBAL_DEFINES += LK_HEAP_IMPLEMENTATION=$(LK_HEAP_IMPLEMENTATION)

include make/module.mk
