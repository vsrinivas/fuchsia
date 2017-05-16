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
	-Isystem/ulib/merkle/include \
	-Isystem/ulib/mxalloc/include \
	-Isystem/ulib/mxtl/include

MODULE_SRCS += \
	system/ulib/merkle/digest.cpp \
	system/ulib/merkle/tree.cpp \
	system/ulib/mxalloc/alloc_checker.cpp \
	$(LOCAL_DIR)/merkleroot.cpp

ifneq (,$(wildcard $(OPENSSL_DIR)/sha.h))
MODULE_DEFINES += USE_LIBCRYPTO=1
MODULE_HOST_LIBS := -lcrypto
else
MODULE_COMPILEFLAGS += -Ithird_party/ulib/cryptolib/include
MODULE_SRCS += third_party/ulib/cryptolib/cryptolib.c
endif

include make/module.mk
