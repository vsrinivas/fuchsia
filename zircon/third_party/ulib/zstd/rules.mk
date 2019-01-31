# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

SHARED_SRCS := \
    $(LOCAL_DIR)/lib/common/debug.c \
    $(LOCAL_DIR)/lib/common/entropy_common.c \
    $(LOCAL_DIR)/lib/common/error_private.c \
    $(LOCAL_DIR)/lib/common/fse_decompress.c \
    $(LOCAL_DIR)/lib/common/pool.c \
    $(LOCAL_DIR)/lib/common/threading.c \
    $(LOCAL_DIR)/lib/common/xxhash.c \
    $(LOCAL_DIR)/lib/common/zstd_common.c \
    $(LOCAL_DIR)/lib/compress/fse_compress.c \
    $(LOCAL_DIR)/lib/compress/hist.c \
    $(LOCAL_DIR)/lib/compress/huf_compress.c \
    $(LOCAL_DIR)/lib/compress/zstd_compress.c \
    $(LOCAL_DIR)/lib/compress/zstd_double_fast.c \
    $(LOCAL_DIR)/lib/compress/zstd_fast.c \
    $(LOCAL_DIR)/lib/compress/zstd_lazy.c \
    $(LOCAL_DIR)/lib/compress/zstd_ldm.c \
    $(LOCAL_DIR)/lib/compress/zstdmt_compress.c \
    $(LOCAL_DIR)/lib/compress/zstd_opt.c \
    $(LOCAL_DIR)/lib/decompress/huf_decompress.c \
    $(LOCAL_DIR)/lib/decompress/zstd_ddict.c \
    $(LOCAL_DIR)/lib/decompress/zstd_decompress_block.c \
    $(LOCAL_DIR)/lib/decompress/zstd_decompress.c \

SHARED_CFLAGS := \
    -I$(LOCAL_DIR)/include/zstd \
    -I$(LOCAL_DIR)/lib/common \
    -O3 -DXXH_NAMESPACE=ZSTD_ \
    -Wno-implicit-fallthrough \

# userlib
MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += $(SHARED_SRCS)

MODULE_LIBS := system/ulib/c

MODULE_COMPILEFLAGS += $(SHARED_CFLAGS)

include make/module.mk

# hostlib
MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS := $(SHARED_SRCS)

MODULE_COMPILEFLAGS += $(SHARED_CFLAGS)

include make/module.mk
