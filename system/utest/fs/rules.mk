# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := fs

MODULE_NAME := fs-test

MODULE_SRCS := \
    $(LOCAL_DIR)/filesystems.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/misc.c \
    $(LOCAL_DIR)/wrap.c \
    $(LOCAL_DIR)/test-access.cpp \
    $(LOCAL_DIR)/test-attr.c \
    $(LOCAL_DIR)/test-append.c \
    $(LOCAL_DIR)/test-basic.c \
    $(LOCAL_DIR)/test-directory.c \
    $(LOCAL_DIR)/test-dot-dot.c \
    $(LOCAL_DIR)/test-link.c \
    $(LOCAL_DIR)/test-fcntl.cpp \
    $(LOCAL_DIR)/test-maxfile.c \
    $(LOCAL_DIR)/test-mmap.cpp \
    $(LOCAL_DIR)/test-overflow.c \
    $(LOCAL_DIR)/test-persist.cpp \
    $(LOCAL_DIR)/test-random-op.c \
    $(LOCAL_DIR)/test-realpath.cpp \
    $(LOCAL_DIR)/test-rename.c \
    $(LOCAL_DIR)/test-resize.cpp \
    $(LOCAL_DIR)/test-rw-workers.c \
    $(LOCAL_DIR)/test-sparse.cpp \
    $(LOCAL_DIR)/test-sync.c \
    $(LOCAL_DIR)/test-threading.cpp \
    $(LOCAL_DIR)/test-truncate.cpp \
    $(LOCAL_DIR)/test-unlink.cpp \
    $(LOCAL_DIR)/test-utils.cpp \
    $(LOCAL_DIR)/test-vmo.cpp \
    $(LOCAL_DIR)/test-watcher.cpp \

MODULE_LDFLAGS := --wrap open --wrap unlink --wrap stat --wrap mkdir
MODULE_LDFLAGS += --wrap rename --wrap truncate --wrap opendir
MODULE_LDFLAGS += --wrap utimes --wrap link --wrap symlink --wrap rmdir
MODULE_LDFLAGS += --wrap chdir --wrap renameat --wrap realpath --wrap remove

MODULE_STATIC_LIBS := \
    system/ulib/fvm \
    system/ulib/fs \
    system/ulib/gpt \
    system/ulib/digest \
    system/ulib/mxcpp \
    system/ulib/fbl \
    third_party/ulib/cryptolib \

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/c \
    system/ulib/fs-management \
    system/ulib/launchpad \
    system/ulib/magenta \
    system/ulib/unittest \

include make/module.mk
