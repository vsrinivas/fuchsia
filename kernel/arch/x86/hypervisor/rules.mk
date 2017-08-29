# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)
SUBARCH_DIR := $(LOCAL_DIR)/../64

MODULE := $(LOCAL_DIR)

MODULE_SRCS := \
	$(LOCAL_DIR)/guest.cpp \
	$(LOCAL_DIR)/vcpu.cpp \
	$(LOCAL_DIR)/vmexit.cpp \
	$(LOCAL_DIR)/vmx_cpu_state.cpp \
\
	$(SUBARCH_DIR)/hypervisor.S \

include make/module.mk
