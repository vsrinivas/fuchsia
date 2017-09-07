# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS := \
	$(LOCAL_DIR)/guest.cpp \
	$(LOCAL_DIR)/vcpu.cpp \
	$(LOCAL_DIR)/vmexit.cpp \
	$(LOCAL_DIR)/vmx.S \
	$(LOCAL_DIR)/vmx_cpu_state.cpp \

include make/module.mk
