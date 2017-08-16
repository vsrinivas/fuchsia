# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/mtrace.cpp \
	$(LOCAL_DIR)/mtrace-ipm.cpp \
	$(LOCAL_DIR)/mtrace-ipt.cpp

include make/module.mk
