# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostapp

MODULE_COMPILEFLAGS += \
	-Ithird_party/ulib/uboringssl/include \
	-Isystem/ulib/digest/include \
	-Isystem/ulib/zxcpp/include \
	-Isystem/ulib/fbl/include

MODULE_SRCS += \
	$(LOCAL_DIR)/merkleroot.cpp

MODULE_HOST_LIBS := \
	third_party/ulib/uboringssl.hostlib \
	system/ulib/digest.hostlib \
	system/ulib/fbl.hostlib \

include make/module.mk
