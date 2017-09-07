# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/digest.cpp \
    $(LOCAL_DIR)/merkle-tree.cpp

MODULE_SO_NAME := digest
MODULE_LIBS := system/ulib/c

# TODO(aarongreen): cryptolib is FAR too slow for general purpose use.  We'll
# need to use it to bootstrap and verify libcrypto.so before switching to
# BoringSSL's optimized digests.
MODULE_STATIC_LIBS := \
    third_party/ulib/cryptolib \
    system/ulib/mxcpp \
    system/ulib/fbl \

include make/module.mk
