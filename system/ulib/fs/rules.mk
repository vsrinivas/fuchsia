# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

COMMON_SRCS := \
    $(LOCAL_DIR)/block-txn.cpp \
    $(LOCAL_DIR)/vfs.cpp \
    $(LOCAL_DIR)/vnode.cpp \

MODULE_SRCS += \
    $(COMMON_SRCS) \
    $(LOCAL_DIR)/connection.cpp \
    $(LOCAL_DIR)/fvm.cpp \
    $(LOCAL_DIR)/lazy-dir.cpp \
    $(LOCAL_DIR)/managed-vfs.cpp \
    $(LOCAL_DIR)/mount.cpp \
    $(LOCAL_DIR)/pseudo-dir.cpp \
    $(LOCAL_DIR)/pseudo-file.cpp \
    $(LOCAL_DIR)/remote-dir.cpp \
    $(LOCAL_DIR)/service.cpp \
    $(LOCAL_DIR)/synchronous-vfs.cpp \
    $(LOCAL_DIR)/unmount.cpp \
    $(LOCAL_DIR)/vmo-file.cpp \
    $(LOCAL_DIR)/watcher.cpp \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/fidl \
    system/ulib/fidl-utils \
    system/ulib/trace-engine \
    system/ulib/zircon \

MODULE_PACKAGE := src

include make/module.mk

# host fs lib

MODULE_HOST_SRCS := \
    $(COMMON_SRCS)

MODULE_HOST_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/fdio/include \
    -Isystem/ulib/zxcpp/include \

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS := $(MODULE_HOST_SRCS)

MODULE_COMPILEFLAGS := $(MODULE_HOST_COMPILEFLAGS)

MODULE_HEADER_DEPS += system/ulib/zircon-internal

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \

include make/module.mk
