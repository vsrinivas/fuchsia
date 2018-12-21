# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_NAME := ftl

MODULE_SRCS := \
    $(LOCAL_DIR)/ftln/ftln_init.c \
    $(LOCAL_DIR)/ftln/ftln_intrnl.c \
    $(LOCAL_DIR)/ftln/ftln_rd.c \
    $(LOCAL_DIR)/ftln/ftln_util.c \
    $(LOCAL_DIR)/ftln/ndm-driver.cpp \
    $(LOCAL_DIR)/ftln/volume.cpp \
    $(LOCAL_DIR)/ndm/ndm_init.c \
    $(LOCAL_DIR)/ndm/ndm_intrnl.c \
    $(LOCAL_DIR)/ndm/ndm_vols.c \
    $(LOCAL_DIR)/utils/aalloc.c \
    $(LOCAL_DIR)/utils/crc32_tbl.c \
    $(LOCAL_DIR)/utils/fsmem.c \
    $(LOCAL_DIR)/utils/fsys.c \
    $(LOCAL_DIR)/utils/fsysinit.c \
    $(LOCAL_DIR)/utils/ftl_mc.c \
    $(LOCAL_DIR)/utils/semaphore.cpp \
    $(LOCAL_DIR)/utils/sys.c \

MODULE_STATIC_LIBS := \
    system/ulib/backtrace-request \
    system/ulib/fbl \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_COMPILEFLAGS := \
    -I$(LOCAL_DIR) \
    -I$(LOCAL_DIR)/utils \
    -I$(LOCAL_DIR)/inc \
    -I$(LOCAL_DIR)/inc/kprivate \

MODULE_CFLAGS := \
    -Wno-sign-compare \
    -DNDM_DEBUG=1 \

ifeq ($(call TOBOOL,$(USE_CLANG)),false)
# gcc:
MODULE_CFLAGS += -Wno-discarded-qualifiers
endif

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
# clang:
MODULE_CFLAGS += -Wno-incompatible-pointer-types-discards-qualifiers
endif

include make/module.mk
