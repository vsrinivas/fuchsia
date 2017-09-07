# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(aarongreen): This isn't pretty but it works for hosts without the
# headers.  The default value matches the normal install on Linux, e.g. when
# installing libssl-dev on Ubuntu.  Mac doesn't have a "normal" location.
OPENSSL_DIR ?= /usr/include/openssl

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostapp

MODULE_COMPILEFLAGS += \
	-Isystem/ulib/digest/include \
	-Isystem/ulib/mxcpp/include \
	-Isystem/ulib/fbl/include

MODULE_SRCS += \
	system/ulib/digest/digest.cpp \
	system/ulib/digest/merkle-tree.cpp \
	$(LOCAL_DIR)/merkleroot.cpp

MODULE_HOST_LIBS := \
	system/ulib/fbl.hostlib

ifneq (,$(wildcard $(OPENSSL_DIR)/sha.h))
MODULE_DEFINES += USE_LIBCRYPTO=1
MODULE_HOST_SYSLIBS := -lcrypto
else
MODULE_COMPILEFLAGS += -Ithird_party/ulib/cryptolib/include
MODULE_SRCS += third_party/ulib/cryptolib/cryptolib.c
endif

include make/module.mk
