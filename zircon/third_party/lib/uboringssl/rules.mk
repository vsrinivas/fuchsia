# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ULIB_DIR := third_party/ulib/uboringssl
SRC_DIR := $(ULIB_DIR)/crypto

KERNEL_INCLUDES += $(ULIB_DIR)/include

# Kernel doesn't support the FPU operations needed for hardware acceleration.  It also doesn't have
# pthreads, so synchronization MUST be handled explicitly (e.g. see kernel/lib/crypto/prng.cpp).
MODULE_COMPILEFLAGS += \
    -DOPENSSL_NO_ASM \
    -DOPENSSL_NO_THREADS_CORRUPT_MEMORY_AND_LEAK_SECRETS_IF_THREADED \

MODULE_SRCS := \
    $(SRC_DIR)/chacha/chacha.c \
    $(SRC_DIR)/fipsmodule/sha/sha256.c \

include make/module.mk
